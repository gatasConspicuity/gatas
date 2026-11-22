#include <stdio.h>

#include "ace/debug.hpp"
#include "ace/semaphoreguard.hpp"
#include "ace/spinlockguard.hpp"
#include "../adsbdecoder.hpp"

GATAS::PostConstruct ADSBDecoder::postConstruct()
{
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(mutex, "ADSBDecoder_mutex");
    state.fix_errors = false;
    mode_s_init(&state);
    ignoredAirplanes.clear();
    adsbDataCollector.clear();
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

void ADSBDecoder::on_receive(const GATAS::ADSBMessageBinMsg &msg)
{
    processAdsbData(msg.frame.get(), msg.length, msg.ingress, INT16_MIN);
}

void ADSBDecoder::on_receive(const GATAS::RadioRxMsg &msg)
{
    if (msg.protocol == GATAS::ConspicuityProtocol::ADSB)
    {
        processAdsbData(msg.frame.get(), msg.lengthBytes, GATAS::TrafficIngress::Transceiver, msg.rssidBm);
    }
}

void ADSBDecoder::processAdsbData(const uint8_t *data, size_t length, GATAS::TrafficIngress ingress, int16_t rssiDbm)
{
    if (data == nullptr || length != GATAS::ADSBMessageBinMsg::MAX_BINARY_LENGTH)
    {
        return;
    }
    mode_s_msg mm;
    // Two phase decoder to first decode the address.. when not in ignoredAirplanes continue decoding
    mode_s_decode_phase1(&state, &mm, data);

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
        auto usTime = CoreUtils::timeUs32Raw();
        auto positionTime = CoreUtils::timeUs32();
        if (ignoredAirplanes.ifContainsThenUpdate(mm.aa, usTime))
        {
            statistics.totalMsgIgnored += 1;
            return;
        }

        mode_s_decode_phase2(&state, &mm);

        if (!adsbDataCollector.start(mm.aa, usTime))
        {
            statistics.knownAircraftFull += 1;
            return;
        }

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
        adsbDataCollector.updateAirborne(true);
        if (mm.metype >= 1 && mm.metype <= 4)
        {
            adsbDataCollector.updateCallsign(mm.flight, mm.aircraft_type);
        }
        else if ((mm.metype >= 9 && mm.metype <= 18) || (mm.metype >= 20 && mm.metype <= 22))
        {
            if (mm.fflag)
            {
                adsbDataCollector.updateRawOdd(mm.raw_latitude, mm.raw_longitude, ingress, rssiDbm);
            }
            else
            {
                adsbDataCollector.updateRawEven(mm.raw_latitude, mm.raw_longitude, ingress, rssiDbm);
            }

            // THis might temporary create a incorrect offset, but should be corrected pretty quickly die to how ADSB sends different information in different packages
            // In addition this is also not the true altitude
            int32_t altitude = (mm.unit == MODE_S_UNIT_METERS ? mm.altitude : mm.altitude * FT_TO_M);

            if (mm.metype >= 20 && mm.metype <= 22) // GPS Altitude so far never seen this
            {
                adsbDataCollector.updateGnssAltitude(altitude);
            }
            else if (mm.metype >= 9 && mm.metype <= 18) // Barometric Altitude
            {
                auto &current = adsbDataCollector.current();
                adsbDataCollector.updateAltitude(altitude + CoreUtils::egmGeoidOffset(current.lat, current.lon));
            }
        }
        else if (mm.metype == 19 && mm.mesub >= 1 && mm.mesub <= 4)
        {
            if (mm.mesub == 1 || mm.mesub == 2)
            {
                // printf("%06lX Ellipsoid:%ldm\n", mm.aa, baro_gnss_diff);
                adsbDataCollector.updateVelocityHeadingBaroDiff(mm.velocity, mm.vert_rate_sign ? -mm.vert_rate : mm.vert_rate, mm.heading, mm.head * FT_TO_M); // mm.head is always in feet
            }
            else if (mm.mesub == 3 || mm.mesub == 4)
            {
                if (mm.heading_is_valid)
                {
                    adsbDataCollector.updateHeading(mm.heading);
                }
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

            // Altitude filtering + Radius filtering
            // to prevent aircraft taking up resources that are not a factor.
            if (outOfAltitudeRange(ownship, current.ellipseHeight) || fromOwn.distance > filterRadius)
            {
                statistics.totalMsgIgnored += 1;
                // printf("Ignored  t:%06ld a:%06lX gnsAlt:%ldm gnsAlt:%0.2fft distance:%ld\n", usTime / 1'000'000, current.icao, current.ellipseHeight, current.ellipseHeight * M_TO_FT, fromOwn.distance);
                if (!ignoredAirplanes.insert(current.icao, usTime))
                {
                    statistics.ignoredAircraftFull += 1;
                }

                current.evict = true;
                return;
            }

            float vertical_rate = current.vert_rate;
            if (vertical_rate > 0)
            {
                // https://mode-s.org/1090mhz/content/ads-b/5-airborne-velocity.html
                vertical_rate = (current.vert_rate - 1) * 64.f * FTPMIN_TO_MS;
            }

            // printf("Received  t:%06ld a:%06lX gnsAlt:%ldm gnsAlt:%0.2fft\n", usTime / 1'000'000, current.icao, current.ellipseHeight, current.ellipseHeight * M_TO_FT);
            getBus().receive(GATAS::IngressAircraftPositionMsg{
                {positionTime - ADSBDECODER_US_DELAY_SERIAL_AND_OVERHEAD,
                 current.callSign,
                 current.icao,
                 GATAS::AddressType::ICAO,
                 current.positionIngress,
                 GATAS::ConspicuityProtocol::ADSB,
                 GATAS::AircraftCategory::UNKNOWN, // TODO: Add support for AircraftCategory
                 false,                            // ADSB does not have privacy
                 false,                            // No privacy
                 current.airborne,
                 current.lat,
                 current.lon,
                 current.ellipseHeight > INT16_MAX ? INT16_MAX : static_cast<int16_t>(current.ellipseHeight),
                 vertical_rate, // https://mode-s.org/decode/content/ads-b/5-airborne-velocity.html,
                 (float)current.velocity * KN_TO_MS,
                 static_cast<int16_t>(current.heading),
                 0.0f,
                 fromOwn.distance,
                 current.squawk}, current.positionRssiDbm});
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
        auto usTime = CoreUtils::timeUs32Raw();
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

GATAS::AircraftCategory ADSBDecoder::getAircraftCategory(const etl::string_view category) const
{
    if (category == "A1")
    {
        return GATAS::AircraftCategory::LIGHT; // < 15500lbs
    }
    else if (category == "A2")
    {
        return GATAS::AircraftCategory::SMALL; // 15500lbs to 75000lbs
    }
    else if (category == "A3")
    {
        return GATAS::AircraftCategory::LARGE; // 75000lbs to 300000lbs
    }
    else if (category == "A4")
    {
        return GATAS::AircraftCategory::HIGH_VORTEX; // Such as Boing 757
    }
    else if (category == "A5")
    {
        return GATAS::AircraftCategory::HEAVY_ICAO; // > 300000lbs
    }
    else if (category == "A6")
    {
        return GATAS::AircraftCategory::AEROBATIC;
    }
    else if (category == "A7")
    {
        return GATAS::AircraftCategory::ROTORCRAFT;
    }
    else if (category == "B1")
    {
        return GATAS::AircraftCategory::GLIDER;
    }
    else if (category == "B2")
    {
        return GATAS::AircraftCategory::LIGHT_THAN_AIR;
    }
    else if (category == "B3")
    {
        return GATAS::AircraftCategory::SKY_DIVER;
    }
    else if (category == "B4")
    {
        return GATAS::AircraftCategory::PARA_GLIDER;
    }
    else if (category == "B6")
    {
        return GATAS::AircraftCategory::UN_MANNED;
    }
    else if (category == "B7")
    {
        return GATAS::AircraftCategory::SPACE_VEHICLE;
    }
    else if (category == "C1")
    {
        return GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE;
    }
    else if (category == "C2")
    {
        return GATAS::AircraftCategory::SURFACE_VEHICLE;
    }
    else if (category == "C3")
    {
        return GATAS::AircraftCategory::POINT_OBSTACLE;
    }
    else if (category == "C4")
    {
        return GATAS::AircraftCategory::CLUSTER_OBSTACLE;
    }
    else if (category == "C5")
    {
        return GATAS::AircraftCategory::LINE_OBSTACLE;
    }
    else
    {
        return GATAS::AircraftCategory::UNKNOWN;
    }
}
