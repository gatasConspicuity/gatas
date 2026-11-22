#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

#include "pico/stdlib.h"

#include "etl/queue_mpmc_mutex.h"
#include "etl/queue_spsc_atomic.h"

#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"

#include "trackerdata.hpp"
#include "ace/antennaRadiationPattern.hpp"

/**
 * Client that can connect to a host and a port and expect to receive line terminated NMEA Messages
 * Part of this code taken from the example from Raspbery
 */
class AircraftTracker : public BaseModule, public etl::message_router<AircraftTracker, GATAS::ConfigUpdatedMsg, GATAS::RadioTxPositionRequestMsg, GATAS::IngressAircraftPositionMsg, GATAS::IngressAircraftPositionsMsg, GATAS::OwnshipPositionMsg, GATAS::Every5SecMsg>
{
private:
    mutable SemaphoreHandle_t trackedAircraftMutex = nullptr;

    friend class message_router;

    static constexpr uint8_t MAX_TRACKING_PLANES = 32;
    static constexpr uint32_t CLEAR_UP_SIZE = (MAX_TRACKING_PLANES * 95) / 100;
    static constexpr uint32_t AUTO_DISTANCE_TRACK_UPPER = (MAX_TRACKING_PLANES * 90) / 100;
    static constexpr uint32_t AUTO_DISTANCE_TRACK_LOWER = (MAX_TRACKING_PLANES * 80) / 100;
    static constexpr uint8_t TIMESLICES = 10;
#if defined(PICO_RP2040)
    // RP2040 memory is tight, so only keep path prediction state for the closest tracks.
    static constexpr uint8_t MAX_PREDICTED_AIRCRAFT = 6;
#else
    static constexpr uint8_t MAX_PREDICTED_AIRCRAFT = MAX_TRACKING_PLANES;
#endif
    struct
    {
        uint32_t queueFullErr = 0; // Might mean ther eis to much pressure on this system and the queue needs to be increased in size. But this will never work if trackedFullErr is also increasing
        uint32_t positionsProcessed = 0;
    } statistics;

private:
    struct TrackInfo
    {
        uint32_t nextSendTime;
        uint8_t numberOfTries;
        GATAS::AircraftPositionInfo position;
        bool operator<(const TrackInfo &other) const
        {
            return nextSendTime < other.nextSendTime;
        }
    };

    struct Tx_Struct
    {
        GATAS::RadioParameters radioParameters;
        uint8_t radioNo;
    };

    TaskHandle_t taskHandle = nullptr;
    TimerHandle_t sendTimerHandle = nullptr;
    TrackerData<MAX_TRACKING_PLANES, TIMESLICES, MAX_PREDICTED_AIRCRAFT> trackedAircraft;
    GATAS::AircraftAddress ownshipAddress;
    bool groundStation = false;

    // Producer Consumer queue to handle data between this task and the send task
    etl::queue_mpmc_mutex<GATAS::AircraftPositionInfo, 16, etl::memory_model::MEMORY_MODEL_SMALL> queue;
    etl::queue_spsc_atomic<Tx_Struct, 1, etl::memory_model::MEMORY_MODEL_SMALL> tXqueue;

    using ProtocolRadPattern = GATAS::AntennaRadiationPattern<GATAS_STATSCOLLECTOR_NUM_RADIALS>;
    etl::array<ProtocolRadPattern, GATAS::RADIO_PROTOCOLS.size()> antennaRadiationPattern;
    const GATAS::OwnshipState &ownshipState;
    bool ownshipPositionValid = false;

    enum TaskState : uint32_t
    {
        TIMER = 1 << 1,
        NEW = 1 << 2,
        MAINTAIN = 1 << 3,
        CLOSEST_10 = 1 << 4
    };

    void on_receive_unknown(const etl::imessage &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::IngressAircraftPositionMsg &msg);
    void on_receive(const GATAS::IngressAircraftPositionsMsg &msg);
    void on_receive(const GATAS::RadioTxPositionRequestMsg &msg);
    void on_receive(const GATAS::Every5SecMsg &msg);
    void on_receive(const GATAS::OwnshipPositionMsg &msg);
    static void aircraftTrackerTrampoline(void *arg);
    static void sendTimerCallback(TimerHandle_t timer);
    void aircraftTrackerTask(void *arg);
    void handleNew();
    void sendEligibleAircraft();
    void closest10();

    void maintenance();

    void handleTrackedAircraft(const GATAS::AircraftPositionInfo &position);
    bool enqueuePosition(const GATAS::AircraftPositionInfo &position);

public:
    static constexpr const etl::string_view NAME = "AircraftTracker";
    AircraftTracker(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), ownshipState(ownshipState_)
    {
        on_receive(GATAS::ConfigUpdatedMsg{config, Configuration::NAME});
    }

    virtual ~AircraftTracker() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
