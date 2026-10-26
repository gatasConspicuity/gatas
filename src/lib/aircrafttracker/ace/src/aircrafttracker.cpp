#include "../aircrafttracker.hpp"
#include "ace/debug.hpp"
#include "ace/coreutils.hpp"
#include "ace/models.hpp"

#include "etl/algorithm.h"

GATAS::PostConstruct AircraftTracker::postConstruct()
{
    trackedAircraftMutex = xSemaphoreCreateMutex();
    if (trackedAircraftMutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(trackedAircraftMutex, "AircraftTracker_mutex");

    sendTimerHandle = xTimerCreate(AircraftTracker::NAME.cbegin(),
                                   TASK_DELAY_MS(1000 / TIMESLICES),
                                   pdTRUE,
                                   this,
                                   sendTimerCallback);
    if (sendTimerHandle == nullptr)
    {
        return GATAS::PostConstruct::TIMER_ERROR;
    }

    return GATAS::PostConstruct::OK;
}

void AircraftTracker::start()
{
    xTaskCreate(aircraftTrackerTrampoline, AircraftTracker::NAME.cbegin(), configMINIMAL_STACK_SIZE + 768, this, tskIDLE_PRIORITY + 2, &taskHandle);
    if (xTimerStart(sendTimerHandle, portMAX_DELAY) != pdPASS)
    {
        GATAS_WARN("AircraftTracker: failed to start send timer");
    }
    getBus().subscribe(*this);
};

void AircraftTracker::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == Configuration::NAME || msg.moduleName == AircraftTracker::NAME)
    {
        auto gaTasConfig = msg.config.gaTasConfig();
        ownshipAddress = gaTasConfig.conspicuity.icaoAddress;
        groundStation = gaTasConfig.conspicuity.groundStation;
        trackedAircraft.ddbEnabled(msg.config.valueByPath(false, NAME, "ddbEnabled"));
        trackedAircraft.pathPrediction(msg.config.valueByPath(false, NAME, "ppEnabled"));
        trackedAircraft.prefixEnabled(msg.config.valueByPath(false, NAME, "prefixEnabled"));
        trackedAircraft.showSquawk(msg.config.valueByPath(false, NAME, "showSquawk"));
    }
}

void AircraftTracker::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void AircraftTracker::on_receive(const GATAS::Every5SecMsg &msg)
{
    (void)msg;
    xTaskNotify(taskHandle, TaskState::MAINTAIN, eSetBits);
}

void AircraftTracker::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    auto guard = SemaphoreGuard<>(portMAX_DELAY, trackedAircraftMutex);
    stream << "{";
    for (uint8_t i = 0; i < static_cast<uint8_t>(GATAS::DataSource::_RADIO); i++)
    {
        stream << "\"" << GATAS::dataSourceIntToString(i) << ":AntPolar\":";
        antennaRadiationPattern[i].serialize(stream);
        stream << ",";
    }
    stream << "\"queueFull:err\":" << statistics.queueFullErr;
    stream << ",\"numberOfObjectsTracking\":" << trackedAircraft.size();
    stream << ",\"positionsProcessed:k\":" << statistics.positionsProcessed;
    stream << ",\"adaptiveRadius:m\":" << trackedAircraft.radius();
    stream << ",\"aircraft:aoa\":{";

    stream << "\"hex\":[";
    bool first = true;
    trackedAircraft.forEachPosition([&](const GATAS::AircraftPositionInfo &aircraft)
                                    {
        if (!first)
        {
            stream << ",";
        }
        first = false;
        stream << "\"";
        CoreUtils::streamIcaoAddress(stream, aircraft.address, aircraft.addressType);
        stream << "\""; });

    stream << "],\"ds\":[";
    first = true;
    trackedAircraft.forEachPosition([&](const GATAS::AircraftPositionInfo &aircraft)
                                    {
        if (!first)
        {
            stream << ",";
        }
        first = false;
        stream << "\"" << GATAS::toString(aircraft.dataSource) << "\""; });

    stream << "],\"dis\":[";
    first = true;
    trackedAircraft.forEachPosition([&](const GATAS::AircraftPositionInfo &aircraft)
                                    {
        if (!first)
        {
            stream << ",";
        }
        first = false;
        stream << aircraft.distanceFromOwn; });
    stream << "]}";
    stream << "}";
}

void AircraftTracker::on_receive(const GATAS::IngressAircraftPositionsMsg &msg)
{
    GATAS_MEASURE("on_receive", 1000);
    auto op = ownshipState.location.load();
    for (const auto &aircraft : msg.positions)
    {
        if (ownshipAddress == aircraft.address)
        {
            continue;
        }
        uint8_t dataSource = static_cast<uint8_t>(aircraft.dataSource);
        if (ownshipPositionValid && dataSource < antennaRadiationPattern.size())
        {
            antennaRadiationPattern[dataSource].put(aircraft, op.lat, op.lon, op.track);
        }
        if (!enqueuePosition(aircraft))
        {
            GATAS_WARN("Queue Full");
            break;
        }
    }
    xTaskNotify(taskHandle, TaskState::NEW, eSetBits);
}

void AircraftTracker::on_receive(const GATAS::OwnshipPositionMsg &)
{
    ownshipPositionValid = true;
}

void AircraftTracker::on_receive(const GATAS::RadioTxPositionRequestMsg &msg)
{
    GATAS_MEASURE("on_receive", 1000);

    // radioParameters.id == 1 means O-Band Uplink
    // We do that here, because we neeed to send 10 aircraft instead of just ownship
    // Only function as ADSL uplink in ground station mode
    if (groundStation && msg.radioParameters.config->dataSource() == GATAS::DataSource::ADSLO_HDR && msg.radioParameters.id == 1)
    {
        if (!tXqueue.full())
        {
            tXqueue.push(Tx_Struct{msg.radioParameters, msg.radioNo});
        }
        xTaskNotify(taskHandle, TaskState::CLOSEST_10, eSetBits);
    }
}

void AircraftTracker::on_receive(const GATAS::IngressAircraftPositionMsg &msg)
{
    if (ownshipAddress == msg.position.address)
    {
        // Ignore ownship so we don't get our own plane on EFB's
        return;
    }

    uint8_t dataSource = static_cast<uint8_t>(msg.position.dataSource);
    auto op = ownshipState.location.load();
    if (ownshipPositionValid && dataSource < antennaRadiationPattern.size())
    {
        antennaRadiationPattern[dataSource].put(msg, op.lat, op.lon, op.track);
    }

    if (!enqueuePosition(msg.position))
    {
        return;
    }
    xTaskNotify(taskHandle, TaskState::NEW, eSetBits);
}

void AircraftTracker::aircraftTrackerTrampoline(void *arg)
{
    static_cast<AircraftTracker *>(arg)->aircraftTrackerTask(arg);
}

void AircraftTracker::sendTimerCallback(TimerHandle_t timer)
{
    auto tracker = static_cast<AircraftTracker *>(pvTimerGetTimerID(timer));
    if (tracker != nullptr && tracker->taskHandle != nullptr)
    {
        xTaskNotify(tracker->taskHandle, TaskState::TIMER, eSetBits);
    }
}

void AircraftTracker::aircraftTrackerTask(void *arg)
{
    (void)arg;
    while (true)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, portMAX_DELAY);
        auto guard = SemaphoreGuard<>(portMAX_DELAY, trackedAircraftMutex);

        // Handle timers
        if (notifyValue & TaskState::MAINTAIN)
        {
            maintenance();
            getBus().receive(GATAS::AdapativeRadiusMsg(trackedAircraft.radius()));
        }

        // Apply new measurements before a simultaneous timer tick predicts output.
        if (notifyValue & TaskState::NEW)
        {
            handleNew();
        }

        if (ownshipPositionValid && (notifyValue & TaskState::TIMER))
        {
            sendEligibleAircraft();
        }

        // Handle timers
        if (ownshipPositionValid && (notifyValue & TaskState::CLOSEST_10))
        {
            closest10();
        }

    }
}

void AircraftTracker::handleNew()
{
    GATAS::AircraftPositionInfo position;
    while (queue.pop(position))
    {
        trackedAircraft.insert(position);
        statistics.positionsProcessed += 1;
    }
}

bool AircraftTracker::enqueuePosition(const GATAS::AircraftPositionInfo &position)
{
    if (queue.push(position))
    {
        return true;
    }

    statistics.queueFullErr += 1;
    return false;
}

void AircraftTracker::handleTrackedAircraft(const GATAS::AircraftPositionInfo &position)
{
    getBus().receive(GATAS::EgressAircraftPositionMsg(position));
}

void AircraftTracker::sendEligibleAircraft()
{
    auto op = ownshipState.location.load();

    trackedAircraft.sendScheduled(
        etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<AircraftTracker, &AircraftTracker::handleTrackedAircraft>(*this), op);
}

void AircraftTracker::closest10()
{
    if (Tx_Struct msg; tXqueue.pop(msg))
    {
        auto op = ownshipState.location.load();
        auto aircraft = trackedAircraft.adslUplinkTrigger(op);
        getBus().receive(GATAS::EgressAircraftPositionsMsg(aircraft, msg.radioParameters, msg.radioNo));
        tXqueue.clear();
    }
}

void AircraftTracker::maintenance()
{
    trackedAircraft.maintenance();
    getBus().receive(GATAS::TrackerStatsMsg{static_cast<uint8_t>(trackedAircraft.size())});
}
