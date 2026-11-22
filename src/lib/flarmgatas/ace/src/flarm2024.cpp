#include <stdio.h>

#include "../flarm2024.hpp"
#include "flarm/flarm2024packet.hpp"
#include "ace/debug.hpp"
#include "ace/spinlockguard.hpp"
#include "ace/measure.hpp"

GATAS::PostConstruct Flarm2024::postConstruct()
{
    return GATAS::PostConstruct::OK;
}

void Flarm2024::start()
{
    getBus().subscribe(*this);
};

void Flarm2024::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    protocolTimeStats.toStream(stream);
    stream << "\"receivedAircraftPositions:k\":" << statistics.receivedAircraftPositions;
    stream << ",\"transmittedAircraftPositions:k\":" << statistics.transmittedAircraftPositions;
    stream << ",\"crc:err\":" << statistics.crcErr;
    stream << ",\"correctedFrames:k\":" << statistics.correctedFrames;
    stream << ",\"outOfDistance:k\":" << statistics.outOfDistance;
    stream << ",\"messageTypeNot0x02:k\":" << statistics.messageTypeNot0x02;
    stream << "}";
}

void Flarm2024::on_receive(const GATAS::RadioRxManchesterMsg &msg)
{

    if (msg.protocol == GATAS::ConspicuityProtocol::FLARM)
    {
        Flarm2024Packet packet;

        auto epochSeconds = CoreUtils::secondsSinceEpoch();
        auto result = packet.loadFromBuffer(epochSeconds, msg.frameSpan(), msg.errorSpan());
        if (result < 0)
        {
            statistics.crcErr += 1;
            return;
        }
        else if (result > 0)
        {
            statistics.correctedFrames += 1;
        }

        if (packet.messageType() != 0x02)
        {
            statistics.messageTypeNot0x02 += 1;
            return;
        }

        auto ownship = ownshipState.location.load();
        if (packet.aircraftId() == ownship.conspicuity.icaoAddress)
        {
            return;
        }

        auto otherPos = packet.getPosition(ownship.lat, ownship.lon);
        auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, otherPos.latitude, otherPos.longitude);

        if (fromOwn.distance > distanceIgnore)
        {
            statistics.outOfDistance += 1;
            return;
        }

        // Only add statistics on valid packets in range
        protocolTimeStats.addReceiveStat(msg.frequency, CoreUtils::msInSecond());

        auto aircraftCat = toAircraftCategory(packet.aircraftType());
        const uint16_t msInMinute = static_cast<uint16_t>((epochSeconds % 60U) * 1'000U);
        auto timeStamp = CoreUtils::timeUs32FromMsInMinute(msInMinute, 15'000);
        if (!timeStamp.has_value())
        {
            GATAS_WARN("Ignoring FLARM position with invalid timestamp: msInMinute=%u",
                       static_cast<unsigned int>(msInMinute));
            return;
        }

        GATAS::IngressAircraftPositionMsg aircraftPosition{
            GATAS::AircraftPositionInfo{
                timeStamp.value(),
                "",
                packet.aircraftId(),
                addressTypeFromFlarm(packet.addressType()),
                GATAS::TrafficIngress::Transceiver,
                GATAS::ConspicuityProtocol::FLARM,
                aircraftCat,
                packet.stealth(),
                packet.noTrack(),
                CoreUtils::isAirborn(aircraftCat, packet.groundSpeed()),
                otherPos.latitude,
                otherPos.longitude,
                packet.altitude(),
                packet.verticalSpeed(),
                packet.groundSpeed(),
                (int16_t)(packet.groundTrack() + 0.5f),
                packet.turnRate(),
                fromOwn.distance},
            msg.rssidBm};

        statistics.receivedAircraftPositions += 1;
        getBus().receive(aircraftPosition);
    }
}

void Flarm2024::on_receive(const GATAS::RadioTxPositionRequestMsg &msg)
{

    if (msg.radioParameters.config->isTxProtocol(GATAS::ConspicuityProtocol::FLARM))
    {
        Flarm2024Packet packet;
        auto epochSeconds = CoreUtils::secondsSinceEpoch();

        auto ownship = ownshipState.location.load();

        packet.aircraftId(ownship.conspicuity.icaoAddress);
        packet.messageType(0x02);
        packet.addressType(static_cast<uint8_t>(ownship.conspicuity.addressType));
        packet.stealth(ownship.conspicuity.stealth);
        packet.noTrack(ownship.conspicuity.noTrack);
        packet.epochSeconds(epochSeconds);
        packet.aircraftType(fromAircraftCategory(ownship.conspicuity.category));
        packet.altitude(etl::clamp(ownship.ellipseHeight, static_cast<int32_t>(-1000), static_cast<int32_t>(11286)));
        packet.setPosition(ownship.lat, ownship.lon);
        packet.turnRate(ownship.hTurnRate);
        packet.groundSpeed(ownship.groundSpeed);
        packet.verticalSpeed(ownship.verticalSpeed);
        packet.groundTrack(ownship.track);
        packet.movementStatus(ownship.airborne ? 2 : 1);

        if (auto frame = static_cast<uint8_t *>(getGlobalPool().alloc(Flarm2024Packet::TOTAL_LENGTH)))
        {
            packet.writeToBuffer(epochSeconds, frame);

            statistics.transmittedAircraftPositions += 1;
            getBus().receive(GATAS::RadioTxFrameMsg{
                getGlobalPool(),
                msg.radioParameters,
                frame,
                Flarm2024Packet::TOTAL_LENGTH,
                msg.radioNo});
        };

        // GATAS_INFO("FLARM request position");
    }
}

GATAS::AddressType Flarm2024::addressTypeFromFlarm(uint8_t addressType) const
{
    constexpr etl::array<GATAS::AddressType, 3> lookupTable =
        {
            GATAS::AddressType::RANDOM, // 0x00
            GATAS::AddressType::ICAO,   // 0x01
            GATAS::AddressType::FLARM,  // 0x02
        };
    if (addressType > 2)
        return GATAS::AddressType::RANDOM;
    return lookupTable[addressType];
}

uint8_t Flarm2024::addressTypeToFlarm(GATAS::AddressType addressType) const
{
    switch (addressType)
    {
    case GATAS::AddressType::ICAO:
        return 0x01;
    case GATAS::AddressType::FLARM:
        return 0x02;
    default:
        return 0;
    }
}

GATAS::AircraftCategory Flarm2024::toAircraftCategory(uint8_t flarmCode) const
{

    // clang-format off
    switch (flarmCode) {
        case 1:    return GATAS::AircraftCategory::GLIDER;
        case 2:
        case 8:    return GATAS::AircraftCategory::LIGHT;
        case 3:    return GATAS::AircraftCategory::ROTORCRAFT;
        case 4:    return GATAS::AircraftCategory::SKY_DIVER;
        case 5:    return GATAS::AircraftCategory::DROP_PLANE;
        case 6:    return GATAS::AircraftCategory::HANG_GLIDER;
        case 7:    return GATAS::AircraftCategory::PARA_GLIDER;
        case 9:    return GATAS::AircraftCategory::LARGE;
        case 0x0b:
        case 0x0c: return GATAS::AircraftCategory::LIGHT_THAN_AIR;
        case 0x0d: return GATAS::AircraftCategory::UN_MANNED;
        case 0x0f: return GATAS::AircraftCategory::POINT_OBSTACLE;
        default:   return GATAS::AircraftCategory::UNKNOWN;
    }
    // clang-format on
}

uint8_t Flarm2024::fromAircraftCategory(GATAS::AircraftCategory category) const
{
    // clang-format off
    switch ( category) {
        case GATAS::AircraftCategory::GLIDER:                  return 0x01;
        case GATAS::AircraftCategory::GYROCOPTER:
        case GATAS::AircraftCategory::ROTORCRAFT:              return 0x03;
        case GATAS::AircraftCategory::SKY_DIVER:               return 0x04;
        case GATAS::AircraftCategory::DROP_PLANE:              return 0x05;
        case GATAS::AircraftCategory::HANG_GLIDER:             return 0x06;
        case GATAS::AircraftCategory::PARA_GLIDER:             return 0x07;
        case GATAS::AircraftCategory::ULTRA_LIGHT_FIXED_WING:
        case GATAS::AircraftCategory::LIGHT:
        case GATAS::AircraftCategory::SMALL:                   return 0x08;
        case GATAS::AircraftCategory::HIGH_VORTEX:
        case GATAS::AircraftCategory::HEAVY_ICAO:
        case GATAS::AircraftCategory::AEROBATIC:
        case GATAS::AircraftCategory::LARGE:                   return 0x09;
        case GATAS::AircraftCategory::LIGHT_THAN_AIR:          return 0x0b;
        case GATAS::AircraftCategory::UN_MANNED:               return 0x0d;
        case GATAS::AircraftCategory::POINT_OBSTACLE:          return 0x0f;

        // Cases with no direct equivalent
        case GATAS::AircraftCategory::SPACE_VEHICLE:
        case GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE:
        case GATAS::AircraftCategory::SURFACE_VEHICLE:
        case GATAS::AircraftCategory::CLUSTER_OBSTACLE:
        case GATAS::AircraftCategory::LINE_OBSTACLE:
        case GATAS::AircraftCategory::MILITARY:
        case GATAS::AircraftCategory::UNKNOWN:
        default: return 0;
    }
    // clang-format on
}
