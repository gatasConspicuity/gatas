#include <stdio.h>

#include "../adslace.hpp"
#include "ace/bitutils.hpp"
#include "ace/debug.hpp"
#include "ace/spinlockguard.hpp"
#include "adsl/adsl.hpp"
#include "ace/moreutils.hpp"
#include "ace/poolallocator.hpp"

constexpr float POSITION_DECODE = 0.0001f / 60.f;

GATAS::PostConstruct ADSLAce::postConstruct()
{
    protocolMutex = xSemaphoreCreateMutex();
    if (protocolMutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(protocolMutex, "ADSLAce_protocolMutex");

    return GATAS::PostConstruct::OK;
}

void ADSLAce::start()
{
    getBus().subscribe(*this);
};

void ADSLAce::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    datasourceTimeStats.toStream(stream);
    stream << "\"receivedAircraftPositions:k\":" << statistics.receivedAircraftPositions;
    stream << ",\"transmittedAircraftPositions:k\":" << statistics.transmittedAircraftPositions;
    stream << ",\"uplinksPacketsSend:k\":" << statistics.uplinksPacketsSend;
    stream << ",\"uplinksPacketsReceived:k\":" << statistics.uplinksPacketsReceived;
    stream << ",\"fec:err\":" << statistics.fecErr;
    stream << ",\"outOfDistance\":" << statistics.outOfDistance;
    stream << ",\"encrypted\":" << statistics.encrypted;
    stream << ",\"protocolVersion:err\":" << statistics.protocolVersionErr;
    stream << ",\"decryptionKey:err\":" << statistics.decryptionKeyErr;
    stream << ",\"payloadUnSupported:err\":" << statistics.payloadUnSupportedErr;
    stream << ",\"unknown:err\":" << statistics.unknownErr;
    stream << ",\"unsupportedFec\":" << statistics.encrypted;
    stream << ",\"TXProtocolVersion\":" << protocol.lowestDominotorProtocolVersion();
    stream << ",\"numberOfNeighbours\":" << protocol.numberOfNeighbours();
    stream << "}";
}

void ADSLAce::on_receive(const GATAS::RadioRxMsg &msg)
{
    if (msg.dataSource != GATAS::DataSource::ADSLO_HDR)
    {
        return;
    }
    protocol.crcCheckOnReceive = true;
    auto status = protocol.handleRx(msg.rssidBm, msg.frame32Span()); // Currently expected that the frame size is always a multiple of 4

    // clang-format off
    switch (status)
    {
        case ADSL::Protocol::RxStatudeCode::OK: {
            datasourceTimeStats.addReceiveStat(msg.frequency, CoreUtils::msInSecond());
            break;
        }
        case ADSL::Protocol::RxStatudeCode::CRC_FAILED:                   { statistics.fecErr += 1; break;}
        case ADSL::Protocol::RxStatudeCode::UNSUPORTED_PROTOCOL_VERSION:  { statistics.protocolVersionErr += 1; break; }
        case ADSL::Protocol::RxStatudeCode::UNSUPORTED_DECRYPTION_KEY:    { statistics.decryptionKeyErr += 1; break; }
        case ADSL::Protocol::RxStatudeCode::UNSUPORTED_ERROR_CONTROL_FEC: { statistics.unsupportedFec += 1; break; }
        case ADSL::Protocol::RxStatudeCode::UNSUPPORTED_PAYLOAD:          { statistics.payloadUnSupportedErr += 1; break; }
        default:                                                          { statistics.unknownErr += 1; break;}
    };
    // clang-format on
}

void ADSLAce::on_receive(const GATAS::RadioRxManchesterMsg &msg)
{
    if (msg.dataSource != GATAS::DataSource::ADSLM)
    {
        return;
    }
    ADSL::Protocol::RxStatudeCode status = ADSL::Protocol::RxStatudeCode::OTHER_ERROR;

    const int check = ADSL::Correct(msg.frameSpan().subspan(1), msg.errorSpan().subspan(1));

    if (check == -1)
    {
        status = ADSL::Protocol::RxStatudeCode::CRC_FAILED;
    }
    else
    {
        protocol.crcCheckOnReceive = false;
        auto frameBytes = msg.frameSpan();
        msg.lengthBytes = frameBytes[0]; // Length it stored on first byte
        etl::mem_move(frameBytes.data() + 1, frameBytes.size() - 1, frameBytes.data());
        status = protocol.handleRx(msg.rssidBm, msg.frame32Span());

        // TODO: change this somehow such that we only show packets that where in range
        datasourceTimeStats.addReceiveStat(msg.frequency, CoreUtils::msInSecond());
    }

    switch (status)
    {
    case ADSL::Protocol::RxStatudeCode::OK:
        break;
    case ADSL::Protocol::RxStatudeCode::CRC_FAILED:
        statistics.fecErr += 1;
        break;
    case ADSL::Protocol::RxStatudeCode::UNSUPORTED_PROTOCOL_VERSION:
        statistics.protocolVersionErr += 1;
        break;
    case ADSL::Protocol::RxStatudeCode::UNSUPORTED_DECRYPTION_KEY:
        statistics.decryptionKeyErr += 1;
        break;
    case ADSL::Protocol::RxStatudeCode::UNSUPORTED_ERROR_CONTROL_FEC:
        statistics.unsupportedFec += 1;
        break;
    case ADSL::Protocol::RxStatudeCode::UNSUPPORTED_PAYLOAD:
        statistics.payloadUnSupportedErr += 1;
        break;
    default:
        statistics.unknownErr += 1;
        break;
    }
}

bool ADSLAce::adsl_sendFrame(const void *ctx, const uint8_t *data, size_t lengthBytes)
{
    const Tx_Struct *params = static_cast<const Tx_Struct *>(ctx);

    getBus().receive(GATAS::RadioTxFrameMsg{
        getGlobalPool(),
        params->radioParameters,
        data,
        lengthBytes,
        params->radioNo});

    return true;
}

void ADSLAce::on_receive(const GATAS::GpsStatsMsg &msg)
{
    gpsStats = msg.gpsStats;
}

void ADSLAce::on_receive(const GATAS::RadioTxPositionRequestMsg &msg)
{
    auto adslm = msg.radioParameters.config->isTxDataSource(GATAS::DataSource::ADSLM);
    auto adslhdr = msg.radioParameters.config->isTxDataSource(GATAS::DataSource::ADSLO_HDR);

    if (!(adslm || adslhdr) || msg.radioParameters.id == 1)
    {
        return;
    }

    auto ownship = ownshipState.location.load();
    auto tp = buildOwnshipTrafficPayload(ownship);

    Tx_Struct *txParams;
    bool addPayloadLength;
    if (adslm)
    {
        rqMBandRadioParameters = Tx_Struct{msg.radioParameters, msg.radioNo};
        txParams = &rqMBandRadioParameters;
        addPayloadLength = true;
    }
    else
    {
        rqOBandRadioParameters = Tx_Struct{msg.radioParameters, msg.radioNo};
        txParams = &rqOBandRadioParameters;
        addPayloadLength = false;
    }

    protocol.tick(ownship.lat, ownship.lon, ownship.ellipseHeight, ownship.groundSpeed, ownship.track);
    protocol.rqSendTrafficPayload(txParams, addPayloadLength, tp);
    statistics.transmittedAircraftPositions += 1;
}

void ADSLAce::on_receive(const GATAS::EgressAircraftPositionsMsg &msg)
{
    if (msg.positions.empty()) {
        return;
    }

    etl::vector<ADSL::UplinkEntry, GATAS::IngressAircraftPositionsMsg::MAX_POSITIONS> traffic={};
    for (auto &&t : msg.positions)
    {
        if (!traffic.full())
        {
            traffic.emplace_back(0x05, t.address, this->buildTrafficPayload(t));
        } else {
            GATAS_WARN("traffic queue full");
        }
    }

    rqOBandRadioParameters = Tx_Struct{msg.radioParameters, msg.radioNo};
    protocol.rqSendUplinkPayload(&rqOBandRadioParameters, false, traffic);
    statistics.uplinksPacketsSend += 1;
}

// Called when a payload/header/status is received by the protocol layer
void ADSLAce::adsl_receivedTraffic(const ADSL::Header &header, const ADSL::TrafficPayload &tp)
{
    auto ownship = ownshipState.location.load();
    statistics.receivedAircraftPositions += 1;

    // Own Address
    if (header.address().asUint() == ownship.conspicuity.icaoAddress)
    {
        return;
    }

    auto latitude = tp.latitude();
    auto longitude = tp.longitude();
    auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, tp.latitude(), tp.longitude());

    if (fromOwn.distance > distanceIgnore)
    {
        statistics.outOfDistance += 1;
        return;
    }
    // printf("ADSL: address:%06X latitude:%0.6f longitude:%0.6f altitude:%ld climbRate:%0.2f speed:%0.2f heading:%0.2f \n",
    // packet.address, fLatitude, fLongitude, packet.getaltitudeGeoid(), packet.getVerticalRate(), packet.getGroundSpeed(), packet.getTrack());
    auto epochMs = CoreUtils::msSinceEpoch();
    auto msElapsed = static_cast<int32_t>(epochMs - tp.timestampRestored(epochMs));
    GATAS::IngressAircraftPositionMsg aircraftPosition{
        GATAS::AircraftPositionInfo{
            CoreUtils::timeUs32() - (msElapsed * 1000),
            "",
            header.address().asUint(),
            addressMapToAddressType(header.addressMappingTable()),
            GATAS::DataSource::ADSLM,
            mapAircraftCategory(tp.aircraftCategory()),
            false,
            false,
            tp.flightState() == ADSL::TrafficPayload::FlightState::AIRBORNE ? true : false, // airBorn
            latitude,
            longitude,
            tp.altitude(),
            tp.verticalRate(),
            tp.speed(),
            static_cast<int16_t>(tp.groundTrack()),
            0.f,
            fromOwn.distance},
        0};
    getBus().receive(aircraftPosition);
    (void)tp;
}

void ADSLAce::adsl_receivedStatus(const ADSL::Header &header, const ADSL::StatusPayload &sp)
{
    (void)sp;
    (void)header;
    GATAS_INFO("Received Status Payload");
}

void ADSLAce::adsl_receivedUplinkTraffic(const ADSL::Header &header, etl::span<const ADSL::UplinkEntry> entries)
{
    (void)header;
    auto ownship = ownshipState.location.load();

    static_assert(ADSL::Protocol::MAX_UPLINK_TARGETS <= GATAS::IngressAircraftPositionsMsg::MAX_POSITIONS);
    // Build directly into the owning message: a separate vector would keep
    // two full ten-aircraft batches on the receive task's stack during dispatch.
    GATAS::IngressAircraftPositionsMsg aircraftPositions;
    auto &positions = aircraftPositions.positions;
    statistics.uplinksPacketsReceived += 1;
    for (const auto &entry : entries)
    {
        const auto &tp = entry.payload();
        if (entry.address().asUint() == ownship.conspicuity.icaoAddress)
        {
            continue;
        }
        auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, tp.latitude(), tp.longitude());
        // if (fromOwn.distance > distanceIgnore)
        // {
        //     statistics.outOfDistance += 1;
        //     continue;
        // }
        auto epochMs = CoreUtils::msSinceEpoch();
        auto msElapsed = static_cast<int32_t>(epochMs - tp.timestampRestored(epochMs));

        if (positions.full())
        {
            break;
        }
        positions.push_back(GATAS::AircraftPositionInfo{
            CoreUtils::timeUs32() - (msElapsed * 1000),
            "",
            entry.address().asUint(),
            addressMapToAddressType(entry.addressMappingTable()),
            GATAS::DataSource::ADSLO_HDR,
            mapAircraftCategory(tp.aircraftCategory()),
            false,
            false,
            tp.flightState() == ADSL::TrafficPayload::FlightState::AIRBORNE,
            tp.latitude(),
            tp.longitude(),
            tp.altitude(),
            tp.verticalRate(),
            tp.speed(),
            static_cast<int16_t>(tp.groundTrack()),
            0.f,
            fromOwn.distance});
    }

    if (!positions.empty())
    {
        statistics.receivedAircraftPositions += positions.size();
        getBus().receive(aircraftPositions);
    }
}

ADSL::TrafficPayload ADSLAce::buildOwnshipTrafficPayload(const GATAS::OwnshipPositionInfo &ownship)
{
    using ADSL::TrafficPayload;
    TrafficPayload tp;

    protocol.setAddress(ownship.conspicuity.icaoAddress);

    tp.timestamp(CoreUtils::msSinceEpoch());
    tp.latitude(ownship.lat);
    tp.longitude(ownship.lon);
    tp.speed(ownship.groundSpeed);
    tp.altitude(ownship.ellipseHeight);
    tp.verticalRate(ownship.verticalSpeed);
    tp.groundTrack(ownship.track);
    tp.flightState(ownship.groundSpeed > GATAS::GROUNDSPEED_CONSIDERING_AIRBORN ? TrafficPayload::FlightState::AIRBORNE : TrafficPayload::FlightState::ON_GROUND);
    tp.aircraftCategory(mapAircraftCategory(ownship.conspicuity.category));

    constexpr float uereDGPS = 2.7f;
    constexpr float uereGPS = 7.0f;
    auto uere = gpsStats.gpsFix.fixType == GATAS::GpsFixType::DGPS ? uereDGPS : uereGPS;
    float hfom = gpsStats.hDop > 0.0f ? gpsStats.hDop * uere : -1.0f;
    float vfom = gpsStats.vDop > 0.0f ? gpsStats.vDop * uere : -1.0f;
    float hpl = gpsStats.pDop > 0.0f ? gpsStats.pDop * uere * 2.0f : -1.0f;

    tp.navigationIntegrity(mapHPL(hpl));
    tp.horizontalPositionAccuracy(mapHFOM(hfom));
    tp.verticalPositionAccuracy(mapVFOM(vfom));
    return tp;
}

ADSL::TrafficPayload ADSLAce::buildTrafficPayload(const GATAS::AircraftPositionInfo &traffic)
{
    using ADSL::TrafficPayload;
    TrafficPayload tp;

    protocol.setAddress(traffic.address);

    tp.timestamp(CoreUtils::msSinceEpoch());
    tp.latitude(traffic.lat);
    tp.longitude(traffic.lon);
    tp.speed(traffic.groundSpeed);
    tp.altitude(traffic.ellipseHeight);
    tp.verticalRate(traffic.verticalSpeed);
    tp.groundTrack(traffic.track);
    tp.flightState(traffic.groundSpeed > GATAS::GROUNDSPEED_CONSIDERING_AIRBORN ? TrafficPayload::FlightState::AIRBORNE : TrafficPayload::FlightState::ON_GROUND);
    tp.aircraftCategory(mapAircraftCategory(traffic.aircraftType));

    constexpr float uereDGPS = 2.7f;
    constexpr float uereGPS = 7.0f;
    auto uere = gpsStats.gpsFix.fixType == GATAS::GpsFixType::DGPS ? uereDGPS : uereGPS;
    float hfom = gpsStats.hDop > 0.0f ? gpsStats.hDop * uere : -1.0f;
    float vfom = gpsStats.vDop > 0.0f ? gpsStats.vDop * uere : -1.0f;
    float hpl = gpsStats.pDop > 0.0f ? gpsStats.pDop * uere * 2.0f : -1.0f;

    tp.navigationIntegrity(mapHPL(hpl));
    tp.horizontalPositionAccuracy(mapHFOM(hfom));
    tp.verticalPositionAccuracy(mapVFOM(vfom));
    return tp;
}

void ADSLAce::adsl_lock()
{
    xSemaphoreTake(protocolMutex, portMAX_DELAY);
}

void ADSLAce::adsl_unlock()
{
    xSemaphoreGive(protocolMutex);
}

void ADSLAce::adsl_buildStatusPayload(const void *ctx, ADSL::StatusPayload &sp)
{
    (void)ctx;
    sp.mBandReceiveCapability(ADSL::StatusPayload::ReceiveCapability::Partial);
    sp.oBandHdrReceiveCapability(ADSL::StatusPayload::ReceiveCapability::Partial);
    sp.oBandHdrReceiveCapability(ADSL::StatusPayload::ReceiveCapability::Partial);
    sp.adslTrafficUplinkClient(true);

    // TODO: Find a way to correctly know what is ocnfigured for the current aircraft
    sp.eReceiveConspicuityBits(ADSL::StatusPayload::EConspicuityBits::EC_FANET);
    sp.eReceiveConspicuityBits(ADSL::StatusPayload::EConspicuityBits::EC_FLARM);
    sp.eReceiveConspicuityBits(ADSL::StatusPayload::EConspicuityBits::EC_OGN);
    sp.eTransmitConspicuityBits(ADSL::StatusPayload::EConspicuityBits::EC_FANET);
    sp.eTransmitConspicuityBits(ADSL::StatusPayload::EConspicuityBits::EC_FLARM);
    sp.eTransmitConspicuityBits(ADSL::StatusPayload::EConspicuityBits::EC_OGN);
}

uint8_t *ADSLAce::adsl_alloc(const void *ctx, size_t sizeBytes)
{
    (void)ctx;
    return static_cast<uint8_t *>(getGlobalPool().alloc(sizeBytes));
}

ADSL::TrafficPayload::AircraftCategory ADSLAce::mapAircraftCategory(GATAS::AircraftCategory category)
{
    using A = ADSL::TrafficPayload::AircraftCategory;
    using B = GATAS::AircraftCategory;

    // clang-format off
    switch (category)
    {
        case B::UNKNOWN:                  return A::NO_INFO;
        case B::LIGHT:                    return A::LIGHT_FIXED_WING;
        case B::SMALL:
        case B::LARGE:
        case B::HIGH_VORTEX:
        case B::HEAVY_ICAO:               return A::SMALL_TO_HEAVY_FIXED_WING;

        case B::AEROBATIC:                return A::LIGHT_FIXED_WING;
        case B::ROTORCRAFT:               return A::LIGHT_ROTORCRAFT;

        case B::GLIDER:                   return A::GLIDER_SAILPLANE;
        case B::LIGHT_THAN_AIR:           return A::LIGHTER_THAN_AIR;
        case B::SKY_DIVER:                return A::PARACHUTIST;
        case B::ULTRA_LIGHT_FIXED_WING:   return A::MODEL_PLANE;

        case B::UN_MANNED:                return A::UAS_OPEN_CATEGORY;
        case B::GYROCOPTER:               return A::GYROCOPTER;
        case B::HANG_GLIDER:              return A::HANGGLIDER;
        case B::PARA_GLIDER:              return A::PARAGLIDER;
        case B::POINT_OBSTACLE:           return A::STATIC_OBJECT;

        default:
            return A::NO_INFO;
    }
    // clang-format on
}

GATAS::AircraftCategory ADSLAce::mapAircraftCategory(ADSL::TrafficPayload::AircraftCategory category)
{
    using A = ADSL::TrafficPayload::AircraftCategory;
    using B = GATAS::AircraftCategory;

    // clang-format off
    switch (category)
    {
        case A::NO_INFO:                   return B::POINT_OBSTACLE;
        case A::LIGHT_FIXED_WING:          return B::LIGHT;
        case A::SMALL_TO_HEAVY_FIXED_WING: return B::SMALL;   // lossy
        case A::LIGHT_ROTORCRAFT:          return B::ROTORCRAFT;
        case A::HEAVY_ROTORCRAFT:          return B::ROTORCRAFT;
        case A::GLIDER_SAILPLANE:          return B::GLIDER;
        case A::LIGHTER_THAN_AIR:          return B::LIGHT_THAN_AIR;
        case A::ULTRALIGHT_HANGGLIDER:     return B::HANG_GLIDER;
        case A::HANGGLIDER:                return B::HANG_GLIDER;
        case A::PARAGLIDER:                return B::PARA_GLIDER;
        case A::PARACHUTIST:               return B::SKY_DIVER;
        case A::GYROCOPTER:                return B::GYROCOPTER;
        case A::MODEL_PLANE:               return B::ULTRA_LIGHT_FIXED_WING;
        case A::STATIC_OBJECT:             return B::POINT_OBSTACLE;

        case A::UAS_OPEN_CATEGORY:
        case A::UAS_SPECIFIC_CATEGORY:
        case A::UAS_CERTIFIED_CATEGORY:
            return B::UN_MANNED;

        case A::EVTOL_UAM:                return B::ROTORCRAFT; // closest lie
        case A::PARAMOTOR:                return B::PARA_GLIDER;

        default:
            return B::UNKNOWN;
    }
    // clang-format on
}

ADSL::TrafficPayload::HorizontalPositionAccuracy ADSLAce::mapHFOM(float hfomMeters)
{
    using H = ADSL::TrafficPayload::HorizontalPositionAccuracy;

    // clang-format off
  if (hfomMeters < 0.0f) { return H::UNKNOWN_NO_FIX; }
  if (hfomMeters < 3.0f)        { return H::HFOM_LESS_3M; }
  if (hfomMeters < 10.0f)       { return H::HFOM_3_10M; }
  if (hfomMeters < 30.0f)       { return H::HFOM_10_30M; }
  if (hfomMeters < 0.05f * 1852.0f) { return H::HFOM_30M_0_05NM; } // 0.05 NM ≈ 92.6 m
  if (hfomMeters < 0.1f  * 1852.0f) { return H::HFOM_0_05_0_1NM; }
  if (hfomMeters < 0.3f  * 1852.0f) { return H::HFOM_0_1_0_3NM; }
  if (hfomMeters < 0.5f  * 1852.0f) { return H::HFOM_0_3_0_5NM; }
    // clang-format on
    return H::UNKNOWN_NO_FIX;
}

ADSL::TrafficPayload::VerticalPositionAccuracy ADSLAce::mapVFOM(float vfomMeters)
{
    using V = ADSL::TrafficPayload::VerticalPositionAccuracy;
    // clang-format off
  if (vfomMeters < 0.0f) { return V::UNKNOWN_NO_FIX; }
  if (vfomMeters < 10.0f)  { return V::VFOM_LESS_10M; }
  if (vfomMeters < 45.0f)  { return V::VFOM_10_45M; }
  if (vfomMeters < 150.0f) { return V::VFOM_45_150M; }
    // clang-format on
    return V::UNKNOWN_NO_FIX;
}

ADSL::TrafficPayload::NavigationIntegrity ADSLAce::mapHPL(float hplMeters)
{
    using N = ADSL::TrafficPayload::NavigationIntegrity;
    // clang-format off
  if (hplMeters < 0.0f) { return N::UNDEFINED; }
  if (hplMeters < 7.5f)    { return N::RC_LESS_7_5M; }
  if (hplMeters < 25.0f)   { return N::RC_7_5_25M; }
  if (hplMeters < 75.0f)   { return N::RC_25_75M; }
  if (hplMeters < 185.2f)  { return N::RC_75M_0_1NM; }
  if (hplMeters < 370.4f)  { return N::RC_0_1_0_2NM; }
  if (hplMeters < 1111.2f) { return N::RC_0_2_0_6NM; }
  if (hplMeters < 1852.0f) { return N::RC_0_6_1NM; }
  if (hplMeters < 3704.0f) { return N::RC_1_2NM; }
  if (hplMeters < 7408.0f) { return N::RC_2_4NM; }
  if (hplMeters < 14816.0f){ return N::RC_4_8NM; }
  if (hplMeters < 37040.0f){ return N::RC_8_20NM; }
    // clang-format on
    return N::RC_20NM_OR_MORE;
}

ADSL::TrafficPayload::VelocityAccuracy ADSLAce::mapVelAcc(float accVelMps)
{
    using V = ADSL::TrafficPayload::VelocityAccuracy;
    // clang-format off
  if (accVelMps <= 0.0f) { return V::UNKNOWN_NO_FIX; }
  if (accVelMps < 1.0f)  { return V::ACC_VEL_LESS_1MS; }
  if (accVelMps < 3.0f)  { return V::ACC_VEL_1_3MS; }
  if (accVelMps < 10.0f) { return V::ACC_VEL_3_10MS; }
    // clang-format on
    return V::UNKNOWN_NO_FIX;
}

GATAS::AddressType ADSLAce::addressMapToAddressType(uint8_t addressMap) const
{
    switch (addressMap)
    {
    case 0x00:
        return GATAS::AddressType::RANDOM;
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
        return GATAS::AddressType::RESERVED;
    case 0x05:
        return GATAS::AddressType::ICAO;
    case 0x06:
        return GATAS::AddressType::FLARM;
    case 0x07:
        return GATAS::AddressType::OGN;
    case 0x08:
        return GATAS::AddressType::FANET;
    // Not a bug, if we don't the the type we just say random
    default:
        return GATAS::AddressType::UNKNOWN;
    }
}

uint8_t ADSLAce::addressTypeToAddressMap(GATAS::AddressType addressType)
{
    switch (addressType)
    {
    case GATAS::AddressType::RANDOM:
        return 0x00;
    case GATAS::AddressType::RESERVED:
        return 0x01;
    case GATAS::AddressType::ICAO:
        return 0x05;
    case GATAS::AddressType::FLARM:
        return 0x06;
    case GATAS::AddressType::OGN:
        return 0x07;
    case GATAS::AddressType::FANET:
        return 0x08;
    default:
        return 0x00;
    }
}
