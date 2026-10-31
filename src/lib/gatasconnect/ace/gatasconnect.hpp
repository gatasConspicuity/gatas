#pragma once

#include <stdint.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"

/* ETLCPP */
#include "etl/message_bus.h"

/* GaTas */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/binarymessages.hpp"
#include "cobsstreamhandler.hpp"

#include "ace/spinlockguard.hpp"

/**
 * Core GatasConnect protocol handling. This class prepares and consumes the
 * framed binary protocol while transport specific modules subscribe to
 * GatasConnectTx and publish GatasConnectRx.
 */
class GatasConnect : public BaseModule, public etl::message_router<GatasConnect, GATAS::WifiConnectionStateMsg, GATAS::ConfigUpdatedMsg, GATAS::GpsStatsMsg, GATAS::IngressAircraftPositionMsg, GATAS::GatasConnectRx, GATAS::GdlMsg>
{
    friend class message_router;

    static constexpr uint8_t LOCALCONFIGURATIONCHANGE_HOLD_BACK = 5;
    static constexpr uint32_t UDP_TRAFFIC_TIMEOUT_US = 5'000'000;

    bool hasGpsFix = false;
    uint64_t lastRadioTrafficUs = 0;
    uint8_t localConfigurationUpdateCnt = 0;
    TimerHandle_t requestTimer = nullptr;
    TaskHandle_t workerTaskHandle = nullptr;
    static constexpr uint32_t SEND_REQUEST = 1;
    const GATAS::GatasConnectOutput gatasConnectOutput = GATAS::GatasConnectOutput::UDPAndBluetooth;
    uint32_t lastUdpTrafficUs = 0;
    bool hasUdpTraffic = false;
    // GDL90 over BlueTooth. Needs gatas companion
    bool gdl90BridgeEnabled = false;

    uint32_t icaoAddress = 0;
    uint32_t gatasIp = 0;
    uint32_t pinCode = 0;
    uint64_t gatasId = 0;
    bool groundStation = false;
    GATAS::WifiMode wifiMode = GATAS::WifiMode::NC;

    etl::vector<uint32_t, GATAS::MAX_AIRCRAFT_CONFIG> allIcaoAddresses;
    const GATAS::OwnshipState &ownshipState;
    CobsStreamHandler cobsStreamHandler;

private:
    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    void on_receive_unknown(const etl::imessage &msg);
    void on_receive(const GATAS::WifiConnectionStateMsg &wcs);
    void on_receive(const GATAS::GpsStatsMsg &msg);
    void on_receive(const GATAS::OwnshipPositionMsg &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::IngressAircraftPositionMsg &msg);
    void on_receive(const GATAS::GatasConnectRx &msg);
    void on_receive(const GATAS::GdlMsg &msg);

    static void requestTimerCallbackTrampoline(TimerHandle_t xTimer);
    static void workerTaskTrampoline(void *arg);
    void workerTask();
    void sendOwnshipPosition();
    void sendAircraftConfiguration();

    void getConfig(const Configuration &config);
public:
    static constexpr const char *NAME = "GatasConnect";
    GatasConnect(etl::imessage_bus &bus, Configuration &config, GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), ownshipState(ownshipState_), cobsStreamHandler(bus, config, ownshipState_)
    {
        getConfig(config);
    }

    virtual ~GatasConnect() = default;
};
