#pragma once

#include <stdint.h>

#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"
#include "ace/spinlockguard.hpp"
#include "ace/messages.hpp"
#include "ace/datasourcetimestatstable.hpp"

#include "etl/map.h"
#include "etl/queue_spsc_atomic.h"

#include "fanet/fanet.hpp"

class FanetAce : public BaseModule, public FANET::Connector, public etl::message_router<FanetAce, GATAS::RadioTxPositionRequestMsg, GATAS::RadioRxMsg, GATAS::ConfigUpdatedMsg>
{
    static constexpr uint8_t QUEUE_SIZE = 6;

private:
    friend class message_router;
    static constexpr uint32_t DEFAULT_IGNORE_DISTANCE = 25000;
    static constexpr uint32_t MAX_IGNORE_DISTANCE = 100000;

    enum TaskState : uint32_t
    {
        HANDLETX = 1 << 2,
    };
    struct
    {
        uint32_t send = 0;
        uint32_t received = 0;
        uint32_t outOfDistance = 0;
    } statistics;

    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    static void fanetAceTaskTrampoline(void *arg);
    void fanetAceTask(void *arg);

    void on_receive(const GATAS::RadioTxPositionRequestMsg &msg);
    void on_receive(const GATAS::RadioRxMsg &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);

    virtual uint32_t fanet_getTick() const override;
    virtual bool fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data) override;
    virtual void fanet_ackReceived(uint16_t id) override;

    TaskHandle_t taskHandle = nullptr;
    FANET::Protocol protocol;
    SemaphoreHandle_t mutex = nullptr;
    GATAS::DataSourceTimeStatsTable<3> datasourceTimeStats;

    uint8_t radioNo = 0;
    uint32_t distanceIgnore;

    const GATAS::OwnshipState &ownshipState;
    SynchronizedValue<GATAS::Config::GaTasConfiguration> gaTasConfiguration;
    GATAS::RadioParameters radioParameters;

    GATAS::AircraftCategory mapAircraftCategory(FANET::TrackingPayload::AircraftType category) const;
    FANET::TrackingPayload::AircraftType mapAircraftCategory(GATAS::AircraftCategory category) const;

public:
    static constexpr const etl::string_view NAME = "Fanet";

    FanetAce(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), protocol(this), distanceIgnore(DEFAULT_IGNORE_DISTANCE), ownshipState(ownshipState_), gaTasConfiguration(CoreUtils::sharedSpinLock(), config.gaTasConfig())
    {
        protocol.ownAddress(FANET::Address{config.gaTasConfig().conspicuity.icaoAddress});
        uint32_t di = config.valueByPath(DEFAULT_IGNORE_DISTANCE, NAME, "distanceIgnore");
        distanceIgnore = etl::clamp(di, static_cast<uint32_t>(0), MAX_IGNORE_DISTANCE);
    }

    virtual ~FanetAce() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
