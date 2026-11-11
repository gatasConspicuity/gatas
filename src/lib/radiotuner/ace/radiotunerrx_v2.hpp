#pragma once

#include <stdint.h>

#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"
#include "ace/models.hpp"
#include "ace/eventsync.hpp"
#include "ace/property.hpp"

#include "countryregulations_v2.hpp"

#include "etl/vector.h"
#include "etl/array.h"
#include "etl/algorithm.h"
#include "etl/string.h"
#include "etl/utility.h"

#include "timers.h"

/**
 * This class is responsible for setting up timings to receive data for each protocol
 */
class RadioTunerRx : public BaseModule, public etl::message_router<RadioTunerRx, GATAS::OwnshipPositionMsg, GATAS::IngressAircraftPositionMsg, GATAS::ConfigUpdatedMsg>
{
    using SlotReceive = etl::array<uint16_t, static_cast<uint8_t>(GATAS::DataSource::_TRANSPROTOCOLS)>;
    using AvailableTimingsSpan = etl::span<const CountryRegulations::ProtocolRxTimeSlot *>;

public:
    static constexpr const uint32_t UPDATE_ZONE_REGULATION_EVERY = 30'000'000; // Get new regulatory dataset every XXms
    static constexpr const uint8_t GATAS_RX_OFFSET = 1;
    static constexpr const uint32_t BIT_EVENT_DONE = 1 << 0;
    // Max pre-expanded RxTiming entries per radio.
    // Increased to leave room for traffic-biased duplicate listening windows.
    static constexpr uint8_t MAX_RX_TIMINGS = 64;
    static constexpr uint8_t RE_EVALUATE_DATASOURCES_SEC = 10;
    static constexpr uint32_t RE_EVALUATE_DATASOURCES_USEC = RE_EVALUATE_DATASOURCES_SEC * 1000 * 1000;

    // A single, non-wrapping listening window for one radio.
    // relStart/relEnd are expressed within the 0..1000 ms window of the second
    // identified by secondIndex. Use start()/end() to obtain absolute timings
    // in milliseconds from the start of the repeating cycle.
    struct RxTiming
    {
        const CountryRegulations::ProtocolRxTimeSlot *protocolTimeConfig = nullptr;
        CountryRegulations::Channel channel = CountryRegulations::Channel::NOOP;
        uint16_t relStart = 0;
        uint16_t relEnd = 0;
        uint16_t secondIndex = 0;

        uint16_t start() const { return static_cast<uint16_t>(secondIndex * 1000 + relStart); }
        uint16_t end() const { return static_cast<uint16_t>(secondIndex * 1000 + relEnd); }
    };

private:
    using TimeSlotVector = etl::vector<RxTiming, MAX_RX_TIMINGS>;

    // Each radio will get one task Context to handle
    struct RadioProtocolCtx
    {
        mutable struct
        {
            uint32_t taskActivity = 0;
        } statistics;

        RadioTunerRx *controller;
        uint8_t radioNo;

        //
        TimeSlotVector protocolTimings = {};
        size_t currentProtocolIdx = std::numeric_limits<size_t>::max();
        uint32_t maxTimeMs;

        // Constructor
        RadioProtocolCtx(RadioTunerRx *controller_, uint8_t radioNo_) : controller(controller_), radioNo(radioNo_), protocolTimings()
        {
            clear();
        }

        // Ensure we never copy
        RadioProtocolCtx(const RadioProtocolCtx &) = delete;
        RadioProtocolCtx &operator=(RadioProtocolCtx const &) = delete;

        void clear()
        {
            protocolTimings.clear();
            currentProtocolIdx = std::numeric_limits<size_t>::max();
            maxTimeMs = 0;
        }

        void getData(etl::string_stream &stream) const
        {
            stream << ",\"radio_" << radioNo << ":rrx\":[";
            for (auto it = protocolTimings.begin(); it != protocolTimings.end(); ++it)
            {
                const auto &ts = *it;
                const char *dsName = (ts.protocolTimeConfig != nullptr) ? GATAS::toString(ts.protocolTimeConfig->radioConfig.dataSource()) : "NOOP";
                stream << "{\"ds\":\"" << dsName << "\",\"s\":" << ts.start() << ",\"e\":" << ts.end() << ",\"ch\":" << static_cast<uint8_t>(ts.channel) << "}";
                if (etl::next(it) != protocolTimings.end())
                {
                    stream << ",";
                }
            }
            stream << "]";
            stream << ",\"taskActivityRadio_" << radioNo << ":k\":" << statistics.taskActivity;
        }
    };

    // Keep track if there is any traffic on the datasources
    SlotReceive slotReceive = {};

    // Keep track of one task per each radio
    etl::vector<RadioProtocolCtx, GATAS_MAX_RADIOS> radioCtxList = {};
    // All datasources that needs to be received
    etl::vector<GATAS::DataSourceConfig, static_cast<uint8_t>(GATAS::DataSource::_TRANSPROTOCOLS)> configuredDatasources = {};

    enum TaskState : uint32_t
    {
        BLOCK = 1 << 2,   // Block all processing
        UNBLOCK = 1 << 3, // UnBlock all processing
    };

private:
    friend class message_router;

    // Current zone we are flying in
    Property<CountryRegulations::Zone> currentZone;
    const GATAS::OwnshipState &ownshipState;
    EventSync eventSync = {};
    TaskHandle_t taskHandle = nullptr;

private:
    static void radioTuneTask(void *arg);

    // ******************** Message bus receive handlers ********************
    // Handle receiving of zone information
    void on_receive(const GATAS::OwnshipPositionMsg &msg);
    // handle receiving of datasources
    void on_receive(const GATAS::IngressAircraftPositionMsg &msg);
    // Handle configuration changes to protocols
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive_unknown(const etl::imessage &msg);

public:
    static constexpr const etl::string_view NAME = "RadioTunerRx";

    RadioTunerRx(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME),
                                                                        currentZone(CountryRegulations::Zone::ZONE0), ownshipState(ownshipState_)
    {
        configuredDatasources = config.gaTasConfig().protocols;
        currentZone.set(CountryRegulations::Zone::ZONE0);
    }
    virtual ~RadioTunerRx() = default;

    // Safe only after failed postConstruct(), before start().
    // Failure leaves no task; the member EventSync releases its event group.
    bool canDelete() const override
    {
        return true;
    }

    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

private:
    /**
     * @brief Assign the datasources to each radio. Should only be called after startup or when zone/config changes.
     */
    void assignDataSourcesFromTask();
    void assignDataSourcesWithTrafficBias();
    void assignDataSourcesImpl(etl::span<const uint8_t> receiveBoost);
    void spreadSecondIndex(RadioProtocolCtx &ctx);
    bool belongsToRadio(size_t timingIndex, size_t radioIndex, size_t radioCount)
    {
        return (timingIndex % radioCount) == radioIndex;
    }

    bool appendProtocolTimings(RadioProtocolCtx &ctx, const CountryRegulations::ProtocolRxTimeSlot *timing, uint16_t &secondIndex);

    /**
     * Return true if for this protocol data was received;
     */
    uint8_t hasReceived(GATAS::DataSource ds);
    void clearTrafficStats();

    static bool fillNoop(RxTiming &entry, uint16_t midMs, etl::span<const CountryRegulations::ProtocolRxTimeSlot *> timings, size_t radioCount, size_t radioIdx, bool sameRadio);
    bool blockTasks();
    void releaseTasks();
};
