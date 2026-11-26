#include <stdio.h>

#include "ace/debug.hpp"
#include "ace/semaphoreguard.hpp"
#include "ace/spinlockguard.hpp"
#include "../adsbdecoder.hpp"
#include "../errorCorrect.hpp"

GATAS::PostConstruct ADSBDecoder::postConstruct()
{
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(mutex, "ADSBDecoder_mutex");
    mode_s_init(&state);
    state.fix_errors = false;
    ignoredAirplanes.clear();
    adsbDataCollector.clear();
    lastAdsbOffsetUpdateSeconds = CoreUtils::timeS32();
    return GATAS::PostConstruct::OK;
}

void ADSBDecoder::start()
{
    getBus().subscribe(*this);
};

void ADSBDecoder::getConfiguration(const Configuration &config)
{
    filterAbove = config.valueByPath(true, NAME, "filterAbove");
    filterBelow = config.valueByPath(true, NAME, "filterBelow");
}

void ADSBDecoder::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == ADSBDecoder::NAME)
    {
        getConfiguration(msg.config);
    }
}

void ADSBDecoder::on_receive(const GATAS::RadioRxMsg &msg)
{
    if (msg.protocol == GATAS::ConspicuityProtocol::ADSB)
    {
        processAdsbData(msg.frame.get(), msg.lengthBytes, msg.ingress, msg.rssidBm);
    }
}

void ADSBDecoder::on_receive(const GATAS::RadioRxManchesterMsg &msg)
{
    if (msg.protocol != GATAS::ConspicuityProtocol::ADSB)
    {
        return;
    }

    if (msg.lengthBytes != 7 && msg.lengthBytes != GATAS::MAX_MODE_S_MESSAGE_LENGTH)
    {
        return;
    }

    uint8_t correctedData[ADSB::MAX_MODE_S_PACKET_BYTES]{};
    for (size_t i = 0; i < msg.lengthBytes; ++i)
    {
        correctedData[i] = msg.frame.get()[i];
    }

    const int correctedBits = ADSB::Correct(
        etl::span<uint8_t>(correctedData, msg.lengthBytes),
        msg.errorSpan());
    if (correctedBits > 0)
    {
        statistics.correctedFrames += 1;
    }

    processAdsbData(correctedData, msg.lengthBytes, msg.ingress, msg.rssidBm);
}

void ADSBDecoder::processAdsbData(const uint8_t *data, size_t length, GATAS::TrafficIngress ingress, int16_t rssiDbm)
{
    if (data == nullptr || (length != 7 && length != GATAS::MAX_MODE_S_MESSAGE_LENGTH))
    {
        return;
    }

    uint8_t paddedData[GATAS::MAX_MODE_S_MESSAGE_LENGTH]{};
    for (size_t i = 0; i < length; ++i)
    {
        paddedData[i] = data[i];
    }

    mode_s_msg mm;
    // Two phase decoder to first decode the address.. when not in ignoredAirplanes continue decoding
    mode_s_decode_phase1(&state, &mm, paddedData);

    const size_t expectedLength = ADSB::isLongModeSPacket(mm.msgtype) ? 14 : 7;
    if (length != expectedLength)
    {
        return;
    }

    if (mm.msgtype != 17 && mm.msgtype != 5 && mm.msgtype != 21)
    {
        return;
    }

    if (!mm.crcok)
    {
        statistics.crcErrors += 1;
        return;
    }

    if (auto guard = SemaphoreGuard(10, mutex))
    {
        // printf("Processing  a:%06lX \n", mm.aa);
        auto usTime = CoreUtils::monotonic();
        auto positionTime = CoreUtils::timeUs32();
        if (ignoredAirplanes.ifContainsThenUpdate(mm.aa, usTime))
        {
            statistics.totalMsgIgnored += 1;
            return;
        }

        mode_s_decode_phase2(&state, &mm);

        if (!adsbDataCollector.acceptsIngress(mm.aa, ingress))
        {
            return;
        }

        if (!adsbDataCollector.start(mm.aa, usTime))
        {
            statistics.knownAircraftFull += 1;
            return;
        }

        adsbDataCollector.current().positionIngress = ingress;

        if (mm.msgtype == 5 || mm.msgtype == 21)
        {
            adsbDataCollector.updateSquawk(mm.identity);
            return;
        }

        if (mm.msgtype != 17)
        {
            return;
        }

        /**
         * https://mode-s.org/decode/content/ads-b/1-basics.html
         */
        if (mm.metype >= 1 && mm.metype <= 4)
        {
            adsbDataCollector.updateCallsign(mm.flight, getAircraftCategory(mm.metype, mm.mesub));
        }
        else if (mm.metype == 0 || (mm.metype >= 9 && mm.metype <= 18) || (mm.metype >= 20 && mm.metype <= 22))
        {
            // Type 0 can supply altitude without a horizontal position.
            if (mm.metype != 0)
            {
                adsbDataCollector.current().pressureAlt = adsbDataCollector.pressureAltitudeAt(usTime);
                if (mm.fflag)
                {
                    adsbDataCollector.updateRawOdd(mm.raw_latitude, mm.raw_longitude, rssiDbm);
                }
                else
                {
                    adsbDataCollector.updateRawEven(mm.raw_latitude, mm.raw_longitude, rssiDbm);
                }
            }

            if (mm.altitude != MODE_S_INVALID_ALTITUDE)
            {
                const auto metres = static_cast<int32_t>(mm.altitude * FT_TO_M);
                if (mm.metype >= 20)
                {
                    adsbDataCollector.updateGnssAltitude(metres);
                }
                else
                {
                    adsbDataCollector.updatePressureAltitude(metres);
                }
            }

        }
        else if (mm.metype == 19 && mm.mesub >= 1 && mm.mesub <= 4)
        {
            auto &difference = adsbDataCollector.current().gnssMinusBaroMetres;
            if (mm.gnssMinusBaroFeet == INT16_MIN)
            {
                difference.reset();
            }
            else
            {
                difference = static_cast<int16_t>(mm.gnssMinusBaroFeet * FT_TO_M);
            }
            if (mm.vert_rate != 0)
            {
                const int rateFpm = (mm.vert_rate - 1) * 64;
                adsbDataCollector.current().verticalRateFpm = mm.vert_rate_sign ? -rateFpm : rateFpm;
            }
            // Only ground-vector reports supply true ground track.
            if ((mm.mesub == 1 || mm.mesub == 2) && mm.velocity != MODE_S_INVALID_GROUND_SPEED)
            {
                adsbDataCollector.updateVelocityHeading(mm.velocity, mm.heading);
            }
        }

        // Used to take specific performance measurements
        // auto usDuration = CoreUtils::timeUs32Raw();
        // const static int EVERY = 250;
        // static int count = 0;
        // static uint32_t duration = 0;
        // duration = duration + (usDuration - usSinceBoot);
        // count += 1;
        // if (count % EVERY == 0)
        // {
        //     puts("\033[;H");
        //     printf("\n counter:%d msgCount:%d took:%.2fus\n", count, msgCount, (float)duration / EVERY);
        //     duration = 0;
        //     adsbDataCollector.dump();
        // }
        if (adsbDataCollector.positionUpdatedAndValid())
        {
            statistics.totalMsgReceived += 1;
            auto &current = adsbDataCollector.current();
            auto ownship = ownshipState.location.load();
            auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, current.lat, current.lon);

            const auto ellipseHeight = current.ellipseHeight();
            // Altitude filtering + Radius filtering
            // to prevent aircraft taking up resources that are not a factor.
            if (outOfAltitudeRange(ownship, ellipseHeight) || fromOwn.distance > filterRadius)
            {
                statistics.totalMsgIgnored += 1;
                // printf("Ignored  t:%06ld a:%06lX gnsAlt:%ldm gnsAlt:%0.2fft distance:%ld\n", usTime / 1'000'000, current.icao, current.ellipseHeight, current.ellipseHeight * M_TO_FT, fromOwn.distance);
                if (!ignoredAirplanes.insert(mm.aa, usTime))
                {
                    statistics.ignoredAircraftFull += 1;
                }

                current.markForEviction();
                return;
            }

            const int64_t verticalMetres = static_cast<int64_t>(ellipseHeight) - ownship.ellipseHeight;
            if (current.gnssMinusBaroMetres &&
                    CoreUtils::isReachedS(lastAdsbOffsetUpdateSeconds))
            {
                ownshipState.updateAdsbAltitudeDifference(mm.aa, *current.gnssMinusBaroMetres, fromOwn.distance, verticalMetres);
                lastAdsbOffsetUpdateSeconds = CoreUtils::timeS32() + ADSB_OFFSET_UPDATE_INTERVAL_SECONDS;
            }

            // printf("Received  t:%06ld a:%06lX gnsAlt:%ldm gnsAlt:%0.2fft\n", usTime / 1'000'000, current.icao, current.ellipseHeight, current.ellipseHeight * M_TO_FT);
            GATAS::AircraftPositionInfo position{positionTime - ADSBDECODER_US_DELAY_SERIAL_AND_OVERHEAD,
                current.callSign,
                mm.aa,
                GATAS::AddressType::ICAO,
                current.positionIngress,
                GATAS::ConspicuityProtocol::ADSB,
                current.category,
                false,                           // ADSB does not have privacy
                false,                           // No privacy
                true,
                current.lat,
                current.lon,
                ellipseHeight,
                current.verticalRateFpm * FTPMIN_TO_MS,
                (float)current.velocity * KN_TO_MS,
                static_cast<int16_t>(current.heading),
                0.0f,
                fromOwn.distance,
                current.squawk,
                adsbDataCollector.pressureAltitudeAt(usTime)};
            getBus().receive(GATAS::IngressAircraftPositionMsg{position, current.positionRssiDbm});
        }
    }
    else
    {
        statistics.msgMissed += 1;
    }
}

bool ADSBDecoder::outOfAltitudeRange(const GATAS::OwnshipPositionInfo &opi, int32_t otherellipseHeight)
{
    return (otherellipseHeight - opi.ellipseHeight) > filterAbove || (opi.ellipseHeight - otherellipseHeight) > filterBelow;
}

void ADSBDecoder::on_receive(const GATAS::Every5SecMsg &msg)
{
    (void)msg;
    if (auto guard = SemaphoreGuard(10, mutex))
    {
        auto usTime = CoreUtils::monotonic();
        ignoredAirplanes.evictOldEntries(usTime);
        adsbDataCollector.evictOldEntries(usTime);
    }
}

void ADSBDecoder::on_receive(const GATAS::AdapativeRadiusMsg &msg)
{
    filterRadius = msg.radius;
}

void ADSBDecoder::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"crcErrors\":" << statistics.crcErrors;
    stream << ",\"correctedFrames\":" << statistics.correctedFrames;
    stream << ",\"adsbDataCollectorSize\":" << adsbDataCollector.size();
    stream << ",\"adsbDataCollectorFull\":" << statistics.knownAircraftFull;
    stream << ",\"ignoredAircraftSize\":" << ignoredAirplanes.size();
    stream << ",\"ignoredAircraftFull\":" << statistics.ignoredAircraftFull;
    stream << ",\"totalMsgReceived:k\":" << statistics.totalMsgReceived;
    stream << ",\"totalMsgIgnored:k\":" << statistics.totalMsgIgnored;
    stream << ",\"msgMissed:k\":" << statistics.msgMissed;
    stream << ",\"filterRadius:mi\":" << filterRadius;
    stream << "}";
}

GATAS::AircraftCategory ADSBDecoder::getAircraftCategory(uint8_t typeCode, uint8_t category)
{
    using AC = GATAS::AircraftCategory;
    // DO-260B identification type codes 2, 3 and 4 select sets C, B and A.
    // B4 groups ultralights, hang gliders and paragliders under GDL90 emitter 12.
    static constexpr AC::enum_type categories[][8] = {
        {AC::UNKNOWN, AC::SURFACE_EMERGENCY_VEHICLE, AC::SURFACE_VEHICLE, AC::POINT_OBSTACLE,
         AC::CLUSTER_OBSTACLE, AC::LINE_OBSTACLE, AC::UNKNOWN, AC::UNKNOWN},
        {AC::UNKNOWN, AC::GLIDER, AC::LIGHT_THAN_AIR, AC::SKY_DIVER,
         AC::ULTRA_LIGHT_FIXED_WING, AC::UNKNOWN, AC::UN_MANNED, AC::SPACE_VEHICLE},
        {AC::UNKNOWN, AC::LIGHT, AC::SMALL, AC::LARGE,
         AC::HIGH_VORTEX, AC::HEAVY_ICAO, AC::AEROBATIC, AC::ROTORCRAFT}
    };
    if (typeCode < 2 || typeCode > 4 || category > 7)
    {
        return AC::UNKNOWN;
    }
    return categories[typeCode - 2][category];
}
