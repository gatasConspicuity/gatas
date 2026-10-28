#include <stdio.h>

#include "pico/stdlib.h"

#include "fanet/fanet.hpp"
#include "fanet/packetParser.hpp"

#include "../fanetace.hpp"

#include "ace/coreutils.hpp"
#include "ace/debug.hpp"
#include "ace/semaphoreguard.hpp"
#include "ace/measure.hpp"
#include "ace/spinlockguard.hpp"
#include "ace/poolallocator.hpp"

void FanetAce::start()
{
    xTaskCreate(fanetAceTaskTrampoline, FanetAce::NAME.cbegin(), configMINIMAL_STACK_SIZE + 256, this, tskIDLE_PRIORITY + 2, &taskHandle);
    getBus().subscribe(*this);
};

GATAS::PostConstruct FanetAce::postConstruct()
{
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(mutex, "FanetAce_mutex");

    return GATAS::PostConstruct::OK;
}

void FanetAce::fanetAceTaskTrampoline(void *arg) {
    FanetAce *fanetAce = static_cast<FanetAce *>(arg);
    fanetAce->fanetAceTask(arg);
}

void FanetAce::fanetAceTask(void *arg)
{
    (void)arg;
    uint32_t waitUntil = 100;
    while (true)
    {

        int32_t delay = static_cast<int32_t>(waitUntil - CoreUtils::timeMs32());
        if (delay < 1)
        {
            delay = 1;
        }
        // printf("Delay %ld\n", delay);

        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, TASK_DELAY_MS(static_cast<uint32_t>(delay)));

        if (auto guard = SemaphoreGuard(100, mutex))
        {
            waitUntil = protocol.handleTx();
        }
    }
}

void FanetAce::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == Configuration::NAME)
    {
        const auto config = msg.config.gaTasConfig();
        gaTasConfiguration.store(config);
        protocol.ownAddress(FANET::Address{config.conspicuity.icaoAddress});
    }
}

void FanetAce::on_receive(const GATAS::RadioTxPositionRequestMsg &msg)
{
    if (msg.radioParameters.config->isTxDataSource(GATAS::DataSource::FANET))
    {
        auto ownship = ownshipState.location.load();
        auto gatasConfig = gaTasConfiguration.load();

        FANET::TrackingPayload payload;
        payload.latitude(ownship.lat)
            .longitude(ownship.lon)
            .altitude(static_cast<int16_t>(etl::clamp(ownship.heightMsl(), static_cast<int32_t>(-450), static_cast<int32_t>(5900))))
            .speed(ownship.groundSpeed * MS_TO_KPH)
            .groundTrack(ownship.track)
            .climbRate(ownship.verticalSpeed)
            .tracking(!gatasConfig.conspicuity.noTrack)
            .turnRate(ownship.hTurnRate)
            .aircraftType(mapAircraftCategory(gatasConfig.conspicuity.category));

        auto packet = FANET::Packet<1>()
                          .payload(payload)
                          .forward(true);

        if (auto guard = SemaphoreGuard(10, mutex))
        {
            radioParameters = msg.radioParameters;
            radioNo = msg.radioNo;
            protocol.sendPacket(packet, 0);
        }
        xTaskNotify(taskHandle, TaskState::HANDLETX, eSetBits);
    }
}

uint32_t FanetAce::fanet_getTick() const
{
    return CoreUtils::timeMs32();
}

bool FanetAce::fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data)
{
    (void)data;
    statistics.send += 1;
    radioParameters.codingRate = codingRate;

    if (auto poolData = static_cast<uint8_t *>(getGlobalPool().alloc(data.size())))
    {
        etl::mem_copy(data.cbegin(), data.cend(), poolData);
        getBus().receive(GATAS::RadioTxFrameMsg{
            getGlobalPool(),
            radioParameters,
            poolData,
            data.size(),
            radioNo});
        // GATAS_INFO("FANET request position");
        return true;
    }
    return false;
}

void FanetAce::fanet_ackReceived(uint16_t id)
{
    (void)id;
}

void FanetAce::on_receive(const GATAS::RadioRxMsg &msg)
{
    (void)msg;
    if (msg.dataSource != GATAS::DataSource::FANET)
    {
        return;
    }
    datasourceTimeStats.addReceiveStat(msg.frequency, CoreUtils::msInSecond());

    statistics.received += 1;

    FANET::Header::MessageType messageType;
    auto spanMsg = etl::span<const uint8_t>(msg.frame.get(), msg.lengthBytes);
    if (auto guard = SemaphoreGuard(10, mutex))
    {
        messageType = protocol.handleRx(msg.rssidBm, spanMsg);
    }
    else
    {
        return;
    }

    auto packet = FANET::PacketParser<12>::parse(spanMsg);
    if (packet.source() == protocol.ownAddress())
    {
        return;
    }
    xTaskNotify(taskHandle, TaskState::HANDLETX, eSetBits);
    auto ownship = ownshipState.location.load();

    switch (messageType)
    {
    case FANET::Header::MessageType::TRACKING:
    {
        FANET::TrackingPayload tp = etl::get<FANET::TrackingPayload>(packet.payload().value());
        auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, tp.latitude(), tp.longitude());

        if (fromOwn.distance > distanceIgnore)
        {
            statistics.outOfDistance += 1;
            return;
        }

        auto aircraftCat = mapAircraftCategory(tp.aircraftType());
        auto groundSpeed = tp.speed() * KPH_TO_MS;

        GATAS::IngressAircraftPositionMsg aircraftPosition{
            GATAS::AircraftPositionInfo{
                CoreUtils::timeUs32(),
                GATAS::CallSign{},
                packet.source().asUint(),
                GATAS::AddressType::FANET,
                GATAS::DataSource::FANET,
                mapAircraftCategory(tp.aircraftType()),
                false,
                !tp.tracking(), // FANET uses 'tracking' to indicate it want's to be tracked
                CoreUtils::isAirborn(aircraftCat, groundSpeed),
                tp.latitude(),
                tp.longitude(),
                tp.altitude() + CoreUtils::egmGeoidOffset(tp.latitude(), tp.longitude()),
                tp.climbRate(),
                groundSpeed,
                static_cast<int16_t>(tp.groundTrack()),
                tp.turnRate(),
                fromOwn.distance},
            msg.rssidBm};
        getBus().receive(aircraftPosition);
    }
    break;
    case FANET::Header::MessageType::GROUND_TRACKING:
    {
        FANET::GroundTrackingPayload tp = etl::get<FANET::GroundTrackingPayload>(packet.payload().value());
        auto fromOwn = CoreUtils::getDistanceRelNorthRelEastInt(ownship.lat, ownship.lon, tp.latitude(), tp.longitude());

        if (fromOwn.distance > distanceIgnore)
        {
            statistics.outOfDistance += 1;
            return;
        }

        GATAS::IngressAircraftPositionMsg aircraftPosition{
            GATAS::AircraftPositionInfo{
                CoreUtils::timeUs32(),
                GATAS::CallSign{},
                packet.source().asUint(),
                GATAS::AddressType::FANET,
                GATAS::DataSource::FANET,
                GATAS::AircraftCategory::POINT_OBSTACLE,
                false,
                !tp.tracking(), // FANET uses 'tracking' to indicate it want's to be tracked
                // Controversial! Even though this is a ground tracking, somw EFB's (ForeFLight) may not show this traffic due to the ground glag.
                // By setting this to true we ensure that also this traffic is seen on the EFB
                true,
                tp.latitude(),
                tp.longitude(),
                ownship.heightMsl(),
                0,
                0,
                0,
                0,
                fromOwn.distance},
            msg.rssidBm};
        getBus().receive(aircraftPosition);
    }
    break;
    case FANET::Header::MessageType::ACK:
    case FANET::Header::MessageType::NAME:
    case FANET::Header::MessageType::MESSAGE:
    case FANET::Header::MessageType::SERVICE:
    case FANET::Header::MessageType::LANDMARKS:
    case FANET::Header::MessageType::REMOTE_CONFIG:
    {
    }
    break;
    }
}

void FanetAce::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    datasourceTimeStats.toStream(stream);
    stream << "\"received:k\":" << statistics.received;
    stream << ",\"send:k\":" << statistics.send;
    stream << ",\"outOfDistance\":" << statistics.outOfDistance;
    stream << ",\"neighbors\":" << protocol.neighborTable().size();
    stream << ",\"Pool Size\":" << protocol.pool().getAllocatedBlocks().size();
    stream << ",\"Airtime:msec\":" << protocol.airTime();
    stream << "}";
}

GATAS::AircraftCategory FanetAce::mapAircraftCategory(FANET::TrackingPayload::AircraftType category) const
{
    // map ADSL_Packet::AircraftCategory to GATAS::AircraftCategory
    switch (category)
    {
    case FANET::TrackingPayload::AircraftType::PARAGLIDER:
        return GATAS::AircraftCategory::PARA_GLIDER;
    case FANET::TrackingPayload::AircraftType::HANGLIDER:
        return GATAS::AircraftCategory::HANG_GLIDER;
    case FANET::TrackingPayload::AircraftType::BALLOON:
        return GATAS::AircraftCategory::LIGHT_THAN_AIR;
    case FANET::TrackingPayload::AircraftType::GLIDER:
        return GATAS::AircraftCategory::GLIDER;
    case FANET::TrackingPayload::AircraftType::POWERED_AIRCRAFT:
        return GATAS::AircraftCategory::LIGHT;
    case FANET::TrackingPayload::AircraftType::HELICOPTER:
        return GATAS::AircraftCategory::ROTORCRAFT;
    case FANET::TrackingPayload::AircraftType::UAV:
        return GATAS::AircraftCategory::UN_MANNED;
    default:
        return GATAS::AircraftCategory::UNKNOWN; // Default to Unknown
    }
}

FANET::TrackingPayload::AircraftType FanetAce::mapAircraftCategory(GATAS::AircraftCategory category) const
{
    switch (category)
    {
    case GATAS::AircraftCategory::PARA_GLIDER:
        return FANET::TrackingPayload::AircraftType::PARAGLIDER;
    case GATAS::AircraftCategory::HANG_GLIDER:
        return FANET::TrackingPayload::AircraftType::HANGLIDER;
    case GATAS::AircraftCategory::LIGHT_THAN_AIR:
        return FANET::TrackingPayload::AircraftType::BALLOON;
    case GATAS::AircraftCategory::GLIDER:
        return FANET::TrackingPayload::AircraftType::GLIDER;
    case GATAS::AircraftCategory::ULTRA_LIGHT_FIXED_WING:
    case GATAS::AircraftCategory::LIGHT:
    case GATAS::AircraftCategory::AEROBATIC:
    case GATAS::AircraftCategory::DROP_PLANE:
    case GATAS::AircraftCategory::SMALL:
    case GATAS::AircraftCategory::LARGE:
    case GATAS::AircraftCategory::HEAVY_ICAO:
    case GATAS::AircraftCategory::HIGH_VORTEX:
        return FANET::TrackingPayload::AircraftType::POWERED_AIRCRAFT;
    case GATAS::AircraftCategory::ROTORCRAFT:
    case GATAS::AircraftCategory::GYROCOPTER:
        return FANET::TrackingPayload::AircraftType::HELICOPTER;
    case GATAS::AircraftCategory::UN_MANNED:
    case GATAS::AircraftCategory::SPACE_VEHICLE:
        return FANET::TrackingPayload::AircraftType::UAV;
    default:
        return FANET::TrackingPayload::AircraftType::OTHER;
    }
}
