#include <stdio.h>

#include "../ogn1.hpp"
#include "../ognpacket.hpp"
#include "ace/bitutils.hpp"
#include "ace/debug.hpp"
#include "ace/spinlockguard.hpp"
#include "etl/algorithm.h"

constexpr float POSITION_DECODE = 0.0001f / 60.f;
constexpr float POSITION_ENDECODE = 1.f / POSITION_DECODE;

LDPC_Decoder<Ogn1::OGN_PACKET_LENGTH * 8, 48> Ogn1::decoder; // 1248 bytes, should be

GATAS::PostConstruct Ogn1::postConstruct()
{
    if (sizeof(OGN_Packet) != OGN_PACKET_LENGTH_FEC + 2) // 20byte + FEC == 6 byte + 2 extra for the word that is ignored
    {
        panic("OGN packet is smaller than expected");
        return GATAS::PostConstruct::FAILED;
    }

    return GATAS::PostConstruct::OK;
}

void Ogn1::start()
{
    getBus().subscribe(*this);
};

void Ogn1::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    protocolTimeStats.toStream(stream);
    stream << "\"relay\":[";
    for (uint8_t idx = 0; idx < 4; idx++)
    {
        stream << statistics.relay[idx];
        if (idx < 3)
        {
            stream << ","; // Add a comma between elements
        }
    }
    stream << "],";

    stream << "\"receivedAircraftPositions:k\":" << statistics.receivedAircraftPositions;
    stream << ",\"transmittedAircraftPositions:k\":" << statistics.transmittedAircraftPositions;
    stream << ",\"fec:err\":" << statistics.fecErr;
    stream << ",\"outOfDistance:k\":" << statistics.outOfDistance;
    stream << ",\"encrypted:k\":" << statistics.encrypted;
    stream << ",\"nonPositional:k\":" << statistics.nonPositional;
    stream << "}";
}

uint8_t Ogn1::ErrCount(const uint8_t *err, uint8_t length) // count detected manchester errors
{
    uint8_t count = 0;
    for (uint8_t idx = 0; idx < length; idx++)
    {
        count += Count1s(err[idx]);
    }
    return count;
}

uint8_t Ogn1::ErrCount(const uint8_t *output, const uint8_t *data, const uint8_t *err, uint8_t length) // count errors compared to data corrected by FEC
{
    uint8_t count = 0;
    for (uint8_t idx = 0; idx < length; idx++)
    {
        count += Count1s((uint8_t)((data[idx] ^ output[idx]) & (~err[idx])));
    }
    return count;
}

uint8_t Ogn1::errorCorrect(uint8_t *output, uint8_t *data, uint8_t *err, uint8_t iter)
{

    uint8_t check = 0;
    uint8_t errCount = ErrCount(err, OGN_PACKET_LENGTH); // conunt Manchester decoding errors
    Ogn1::decoder.Input(data, err);                      // put data into the FEC decoder
    do                                                   // more loops is more chance to recover the packet
    {
        check = decoder.ProcessChecks(); // do an iteration
    } while ((iter--) && check); // if FEC all fine: break
    Ogn1::decoder.Output(output); // get corrected bytes into the OGN packet
    errCount += ErrCount(output, data, err, OGN_PACKET_LENGTH);

    errCount = etl::min(errCount, (uint8_t)15);
    check = etl::min(check, (uint8_t)15);
    return (check & 0x0F) | (errCount << 4);
}

GATAS::AddressType Ogn1::addressTypeFromOgn(uint8_t addressType) const
{
    switch (addressType)
    {
    case 0x03:
        return GATAS::AddressType::OGN;
    case 0x02:
        return GATAS::AddressType::FLARM;
    case 0x01:
        return GATAS::AddressType::ICAO;
    default:
        return GATAS::AddressType::RANDOM;
    }
}

uint8_t Ogn1::addressTypeToOgn(GATAS::AddressType addressType) const
{
    constexpr uint8_t lookupTable[] =
        {
            0x00, // RANDOM
            0x01, // ICAO
            0x02, // FLARM
            0x03, // OGN (default to 0x00, update if needed)
            0x00, // FANET (default to 0x00, update if needed)
            0x00, // ADSL (default to 0x00, update if needed)
            0x00, // RESERVED (default to 0x00, update if needed)
            0x00  // UNKNOWN (default to 0x00, update if needed)
        };

    uint8_t index = static_cast<uint8_t>(addressType) & 0x07;
    return lookupTable[index];
}

int8_t Ogn1::parseFrame(OGN_Packet &packet, uint32_t frequency, int16_t rssiDbm)
{
    if (packet.Header.NonPos)
    {
        statistics.nonPositional += 1;
        return 0;
    }
    statistics.relay[packet.Header.Relay % 4] += 1;

    float fLatitude = POSITION_DECODE * packet.DecodeLatitude();
    float fLongitude = POSITION_DECODE * packet.DecodeLongitude();

    auto ownship = ownshipState.location.load();

    auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, fLatitude, fLongitude);

    //    printf("OGN: address:%06X latitude:%0.6f longitude:%0.6f altitude:%ld offset:%d, stdaltitude:%ld climbRate:%d speed:%d heading:%0.2f turnRate:%0.2f\n",
    //    packet.Header.Address, fLatitude, fLongitude, packet.DecodeAltitude() * 10 + ownship.geoidOffset, ownship.geoidOffset, packet.DecodeStdAltitude(), packet.DecodeClimbRate(), packet.DecodeSpeed(), packet.DecodeHeading() * 0.1f, packet.DecodeTurnRate()*0.1f);

    if (fromOwn.distance > distanceIgnore)
    {
        statistics.outOfDistance += 1;
        // printf("Distance %2.f > GATAS_FLARM_IGNORE_DISTANCE_ERRORS, ignoring (%.4f, %.4f, %.4f, %.4f)\n",   distance, fLatitude, fLongitude, lastGpsPosition.latitude, lastGpsPosition.longitude);
        return -1;
    }

    // Only add to polar plot for valid packages within distance
    protocolTimeStats.addReceiveStat(frequency, CoreUtils::msInSecond());

    statistics.receivedAircraftPositions += 1;
    int16_t speed0d1ms = packet.DecodeSpeed();
    auto aircrftCat = ognToGatas(static_cast<Ogn1::OGNAircraftType>(packet.Position.AcftType));
    auto groundSpeed = speed0d1ms * .1f;
    const uint16_t msInMinute = static_cast<uint16_t>(packet.Position.Time * 1'000U);
    auto timeUs32 = CoreUtils::timeUs32FromMsInMinute(msInMinute);
    if (!timeUs32.has_value())
    {
        GATAS_WARN("Ignoring OGN position with invalid timestamp: msInMinute=%u",
                   static_cast<unsigned int>(msInMinute));
        return -1;
    }
    GATAS::IngressAircraftPositionMsg aircraftPosition{
        GATAS::AircraftPositionInfo{
            timeUs32.value(),
            "",
            packet.Header.Address,
            addressTypeFromOgn(packet.Header.AddrType),
            GATAS::TrafficIngress::Transceiver,
            GATAS::ConspicuityProtocol::OGN,
            aircrftCat,
            packet.Position.Stealth ? true : false, // Privacy
            false,                                  // noTrack
            CoreUtils::isAirborn(aircrftCat, groundSpeed),
            fLatitude,
            fLongitude,
            packet.DecodeAltitude() + CoreUtils::egmGeoidOffset(fLatitude, fLongitude), // OGN Sends MSL
            packet.DecodeClimbRate() * .1f,                                             // Climb Rate is send s 0.1m/s (10 means 1/ms)
            groundSpeed,
            static_cast<int16_t>(packet.DecodeHeading() * .1f),
            packet.DecodeTurnRate() * .1f,
            fromOwn.distance},
        rssiDbm};
    getBus().receive(aircraftPosition);
    return 0;
}

void Ogn1::on_receive(const GATAS::RadioTxPositionRequestMsg &msg)
{
    if (msg.radioParameters.config->isTxProtocol(GATAS::ConspicuityProtocol::OGN))
    {
        auto ownship = ownshipState.location.load();

        OGN_Packet packet;
        packet.Header =
            {
                .Address = ownship.conspicuity.icaoAddress, // Address
                .AddrType = addressTypeToOgn(ownship.conspicuity.addressType),
                .NonPos = 0,    // 0 = position packet, 1 = other information like status
                .Parity = 0,    // parity takes into account bits 0..27 thus only the 28 lowest bits
                .Relay = 0,     // 0 = direct packet, 1 = relayed once, 2 = relayed twice, ...
                .Encrypted = 0, // packet is encrypted
                .Emergency = 0, // aircraft in emergency (not used for now)
            };

        packet.calcAddrParity();

        packet.EncodeLatitude(ownship.lat * POSITION_ENDECODE);
        packet.EncodeLongitude(ownship.lon * POSITION_ENDECODE);
        packet.EncodeSpeed(ownship.groundSpeed * 10.f);
        packet.EncodeHeading(ownship.track * 10.f);
        packet.EncodeClimbRate(ownship.verticalSpeed * 10.f);
        packet.EncodeTurnRate(ownship.hTurnRate * 10.f);
        packet.EncodeAltitude(etl::clamp(ownship.heightMsl(), static_cast<int32_t>(0),  static_cast<int32_t>(61432)));
        packet.EncodeDOP(gpsStats.pDop + 0.5f);

        // TODO: Understand how baro Altitude really works in OGN
        packet.clrBaro();
        // if (CoreUtils::msElapsed(lastBarometricPressureMsg.msSinceBoot) > 4'000)
        // {
        //     packet.clrBaro();
        // }
        // else
        // {
        //     packet.EncodeStdAltitude(lastBarometricPressureMsg.pressurehPa);
        // }

        auto msSinceEpoch = CoreUtils::msSinceEpoch();
        tm time = CoreUtils::localTime(msSinceEpoch);
        uint8_t secondTime = time.tm_sec;
        // Round time to nearest full second
        if (CoreUtils::msInSecond() >= 500)
        {
            secondTime += 1;
            if (secondTime >= 60)
            {
                secondTime -= 60;
            }
        }

        uint8_t fixMode;
        uint8_t fixQuality;
        switch (gpsStats.gpsFix.fixType)
        {
        case GATAS::GpsFixType::D3:
            fixMode = 1;
            fixQuality = 1;
            break;

        case GATAS::GpsFixType::DGPS:
            fixMode = 1;
            fixQuality = 2;
            break;

        default:
            fixMode = 0;
            fixQuality = 0;
            break;
        }

        packet.Position.Time = secondTime;
        packet.Position.FixQuality = fixQuality;
        packet.Position.FixMode = fixMode;
        packet.Position.AcftType = gatasToOgn(ownship.conspicuity.category);

        packet.Whiten();
        LDPC_Encode(packet.Word());
        statistics.transmittedAircraftPositions += 1;

        if (auto data = static_cast<uint8_t *>(getGlobalPool().alloc(OGN_PACKET_LENGTH_FEC)))
        {
            etl::mem_copy(reinterpret_cast<uint8_t *>(&packet), OGN_PACKET_LENGTH_FEC, data);

            getBus().receive(GATAS::RadioTxFrameMsg{
                getGlobalPool(),
                msg.radioParameters,
                data,
                OGN_PACKET_LENGTH_FEC,
                msg.radioNo});
            // GATAS_INFO("OGN request position");
        }
    }
}

void Ogn1::on_receive(const GATAS::RadioRxManchesterMsg &msg)
{
    if (msg.protocol == GATAS::ConspicuityProtocol::OGN)
    {
        OGN_Packet packet;

        // Validate packet, and correct if possible
        uint8_t check = Ogn1::errorCorrect((uint8_t *)&packet, msg.frame.get(), msg.error.get());
        if (check & 0x0F)
        {
            statistics.fecErr += 1;
            return;
        }

        packet.Dewhiten();
        if (packet.Header.Encrypted)
        {
            statistics.encrypted += 1;
            return;
        }

        // Ignore ownship address
        auto ownship = ownshipState.location.load();
        if (packet.Header.Address == ownship.conspicuity.icaoAddress)
        {
            return;
        }

        parseFrame(packet, msg.frequency, msg.rssidBm);
    }
}


void Ogn1::on_receive(const GATAS::GpsStatsMsg &msg)
{
    gpsStats = msg.gpsStats;
}

void Ogn1::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

Ogn1::OGNAircraftType Ogn1::gatasToOgn(GATAS::AircraftCategory c) const
{
    using AC = GATAS::AircraftCategory;
    using OGN = Ogn1::OGNAircraftType;

    switch (c)
    {
    case AC::GLIDER:
        return OGN::GLIDER;

    case AC::ROTORCRAFT:
    case AC::GYROCOPTER:
        return OGN::ROTORCRAFT;

    case AC::SKY_DIVER:
        return OGN::SKYDIVER;

    case AC::DROP_PLANE:
        return OGN::DROP_PLANE;

    case AC::HANG_GLIDER:
        return OGN::HANG_GLIDER;

    case AC::PARA_GLIDER:
        return OGN::PARAGLIDER;

    case AC::LIGHT_THAN_AIR:
        return OGN::BALLOON;

    case AC::LIGHT:
    case AC::SMALL:
    case AC::ULTRA_LIGHT_FIXED_WING:
    case AC::AEROBATIC:
        return OGN::RECIP_ENGINE;

    case AC::LARGE:
    case AC::HIGH_VORTEX:
    case AC::HEAVY_ICAO:
    case AC::MILITARY:
        return OGN::JET_TURBOPROP;

    case AC::UN_MANNED:
        return OGN::UAV;

    case AC::POINT_OBSTACLE:
    case AC::CLUSTER_OBSTACLE:
    case AC::LINE_OBSTACLE:
        return OGN::OBSTACLE;

    default:
        return OGN::UNKNOWN;
    }
}

GATAS::AircraftCategory Ogn1::ognToGatas(Ogn1::OGNAircraftType o) const
{
    using AC = GATAS::AircraftCategory;
    using OGN = Ogn1::OGNAircraftType;

    switch (o)
    {
    case OGN::GLIDER:
        return AC::GLIDER;

    case OGN::TOW_PLANE:
        return AC::LIGHT;

    case OGN::ROTORCRAFT:
        return AC::ROTORCRAFT;

    case OGN::SKYDIVER:
        return AC::SKY_DIVER;

    case OGN::DROP_PLANE:
        return AC::DROP_PLANE;

    case OGN::HANG_GLIDER:
        return AC::HANG_GLIDER;

    case OGN::PARAGLIDER:
        return AC::PARA_GLIDER;

    case OGN::RECIP_ENGINE:
        return AC::LIGHT;

    case OGN::JET_TURBOPROP:
        return AC::LARGE;

    case OGN::BALLOON:
        return AC::LIGHT_THAN_AIR;

    case OGN::AIRSHIP:
        return AC::LIGHT_THAN_AIR;

    case OGN::UAV:
        return AC::UN_MANNED;

    case OGN::OBSTACLE:
        return AC::POINT_OBSTACLE;

    default:
        return AC::UNKNOWN;
    }
}
