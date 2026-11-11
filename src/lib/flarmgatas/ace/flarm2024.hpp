#pragma once

// #include "flarm_common.hpp"

/* System. */
#include <stdint.h>
#include <algorithm>

///* PICO. */
// #include "pico/stdlib.h"
// #include "pico/binary_info.h"

/* Vendor. */
#include "etl/message_bus.h"
#include "etl/string.h"

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"
#include "ace/datasourcetimestatstable.hpp"

class Flarm2024 : public BaseModule, public etl::message_router<Flarm2024, GATAS::RadioRxManchesterMsg, GATAS::RadioTxPositionRequestMsg>
{
    friend class message_router;
    static constexpr uint32_t DEFAULT_IGNORE_DISTANCE = 25000;
    static constexpr uint32_t MAX_IGNORE_DISTANCE = 100000;

private:
    struct
    {
        uint32_t receivedAircraftPositions = 0;
        uint32_t transmittedAircraftPositions = 0;
        uint32_t crcErr = 0;
        uint32_t correctedFrames = 0;
        uint32_t outOfDistance = 0;
        uint32_t messageTypeNot0x02 = 0;
    } statistics;

    GATAS::DataSourceTimeStatsTable<2> datasourceTimeStats;

    const GATAS::OwnshipState &ownshipState;
    GATAS::Config::GaTasConfiguration gaTasConfiguration;
    uint32_t distanceIgnore;

public:
    static constexpr const etl::string_view NAME = "Flarm";
    Flarm2024(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), ownshipState(ownshipState_)
    {
        uint32_t di = config.valueByPath(DEFAULT_IGNORE_DISTANCE, NAME, "distanceIgnore");
        distanceIgnore = etl::clamp(di, static_cast<uint32_t>(0), MAX_IGNORE_DISTANCE);
        gaTasConfiguration = config.gaTasConfig();
    }

    // Safe only after failed postConstruct(), before start().
    // Construction and postConstruct only initialise owned state.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~Flarm2024() = default;

    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

private:
    /**
     * Send a FreeRTOS message when a FlarmFrame is received
     * This will release the sender from the task and allow it to continue in a seperate thread
     */
    void on_receive(const GATAS::RadioRxManchesterMsg &msg);
    void on_receive(const GATAS::RadioTxPositionRequestMsg &msg);

    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    // Transform a FLARM addressType to an gaTas address type
    GATAS::AddressType addressTypeFromFlarm(uint8_t addressType) const;

    uint8_t addressTypeToFlarm(GATAS::AddressType) const;

    GATAS::AircraftCategory toAircraftCategory(uint8_t flarmCode) const;
    uint8_t fromAircraftCategory(GATAS::AircraftCategory category) const;
};
