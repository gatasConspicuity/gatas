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

#include "etl/algorithm.h"
#include "etl/utility.h"

#include "timers.h"

/**
 * This class is responsible for timimgs of sending location data for each configured protocol
 */
class RadioTunerTx : public BaseModule, public etl::message_router<RadioTunerTx, GATAS::OwnshipPositionMsg, GATAS::ConfigUpdatedMsg, GATAS::RadioControlMsg>
{
    struct ConspicuityProtocolTxEvent
    {
        const CountryRegulations::ProtocolTxTimeSlot *slot;
        uint32_t atTime = 0;
    };
    enum TaskState : uint32_t
    {
        BLOCK = 1 << 2,   // Block all processing
        UNBLOCK = 1 << 3, // UnBlock all processing
    };

public:
    static constexpr const uint32_t UPDATE_ZONE_REGULATION_EVERY = 30'000'000; // Get new regulatory dataset every XXms
    static constexpr const uint32_t BIT_EVENT_DONE = 1 << 0;

private:
    // Each radio will get one task Context to handle
    struct
    {
        uint32_t taskActivity = 0;
    } statistics;

    void getData(etl::string_stream &stream) const
    {
        (void)stream; // Avoid unused parameter warning
    }

private:
    friend class message_router;
    const GATAS::OwnshipState &ownshipState;

    // Current zone we are flying in
    CountryRegulations::Zone currentZone = CountryRegulations::Zone::ZONE0;
    // True when groundspeed indicates we are airborne; default false (conservative: use static timing)
    bool isAirborne = false;
    // Store slot + 1 so zero represents a protocol with no observed RX assignment.
    etl::array<uint8_t, GATAS::RADIO_PROTOCOLS.size()> preferredRadio = {};
    etl::array<const Radio *, GATAS_MAX_RADIOS> radios = {};
    static constexpr size_t MaxQueueSize = GATAS::RADIO_PROTOCOLS.size();
    etl::vector<ConspicuityProtocolTxEvent, CountryRegulations::MAX_PROTOCOL_TX_TIMINGS> protocolTxEvents;
    etl::vector<GATAS::ProtocolConfig, GATAS::RADIO_PROTOCOLS.size()> configuredProtocols;

    EventSync eventSync;
    TaskHandle_t taskHandle;

private:
    static void radioTuneTaskTrampoline(void *arg);
    void radioTuneTask();
    etl::optional<uint8_t> selectRadio(GATAS::ConspicuityProtocol source, GATAS::RadioCapability capability) const;

    // ******************** Message bus receive handlers ********************
    // Handle receiving of zone information
    void on_receive(const GATAS::OwnshipPositionMsg &msg);
    // Handle configuration changes to protocols
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::RadioControlMsg &msg);
    void on_receive_unknown(const etl::imessage &msg);

public:
    static constexpr const etl::string_view NAME = "RadioTunerTx";

    RadioTunerTx(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), ownshipState(ownshipState_)
    {
        (void)config;
    }
    // Safe only after failed postConstruct(), before start().
    // Failure means no task was created; subscriptions begin in start().
    bool canDelete() const override
    {
        return true;
    }

    virtual ~RadioTunerTx() = default;

    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

private:
    /**
     * @brief Assign the protocols to each radio. THis should only be used and called after startup, or when aircraft is changed
     *
     * @param protocols
     */
    void assignProtocols(const etl::span<GATAS::ProtocolConfig> &protocols);
};
