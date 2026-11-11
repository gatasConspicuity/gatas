#pragma once
#include "FreeRTOS.h"
#include "timers.h"

#include "etl/string.h"
#include "etl/list.h"

#include "ace/basemodule.hpp"
#include "ace/messages.hpp"

#include "pico/cyw43_arch.h"

#include "../dhcpserver/dhcpserver.h"
#include "../dnsserver/dnsserver.h"

/**
 * WifiService handles both Access Point and Client Mode
 * AP Mode: You connect to GATAS
 * Client Mode: Open Ace connects to you
 *
 * It handles up to 4 network clients, process of connecting
 * 1) Scan for networks and match them with configured networks (this means hidden SSID's are currenty not supported)
 * 2) For each network:
 * 3) If the connection fails with bad auth, try next network
 * 4) If connection fails, try again up to 3 times
 * 5) When no client can be connected, setup an Access Point
 *
 * - Future ideas:
 * 1) Allow hidden SSID's
 * 2) Allow to disable an client SSID
 * 3) Allow for client mode only, never set up an AP, just keep trying
 * 4) Open Portal in AP mode
 * 5) Scan the network for any devices (ping?? openPort??) and send GDL90 to these devices found?
 * 6) When in AP mode, restart client mode with a button
 */
class WifiService : public BaseModule, public etl::message_router<WifiService, GATAS::WifiModeRequestMsg>
{
private:
    // 17 Dec Increased from 2 to 4 due to phone of Piet
    static constexpr uint8_t NUMBER_OF_CONNECTION_ATTEMPTS = 4; // Number of times connection to the same network is attempted before trying an other network
    static constexpr uint8_t NUMBER_OF_SCAN_ATTEMPTS = 3;       // Number is scans done and if no known network is found, create the AP

    friend class message_router;
    GATAS::Config::WifiServiceData wifiData;

    struct IpGw
    {
        ip4_addr_t ip;
        ip4_addr_t gateWay;
    };

    enum ConnectionState
    {
        START,
        ENABLECLIENT,

        WIFISCAN,
        WIFISCANNING,

        TRYCLIENTCONNECT,
        CLIENTMODESTARTED,
        CLIENT_TO_AP,

        AP_TO_CLIENT,
        APMODESTART,
        APSTARTED
    };

    ConnectionState connectionState;

    dhcp_server_t dhcp_server;
    dns_server_t dns_server;

    TaskHandle_t taskHandle;
    TimerHandle_t timerHandle;

    uint8_t totalScanAttempt;
    GATAS::WifiMode wifiMode;
    GATAS::WifiMode requestedWifiMode;
    s8_t mdnsSlot;
    // Keep track of current status, to prevent sending messages every XX seconds
    bool wifiStatePublishPending;
    // GATAS will stay in client mode but only after it first successfully connected to an AP
    // When successClientConnected was set to true, a client connection was successful
    bool successClientConnected;
    // When set to true, don't scan for networks, just try to connect to teh configured networks.
    // This is usefull for hidden networks or hotspots that don't send SSD regurly.
    // The downside is that connection is a bit slow when multiple netwprks are configured
    bool dontScanJustConnectToClient;
    // set of all known networks found during scan
    struct ScanResultT
    {
        GATAS::SsidOrPasswdStr ssid;
        uint8_t connectAttempt;
        ScanResultT(const GATAS::SsidOrPasswdStr &_ssdPwd) : ssid(_ssdPwd), connectAttempt(1) {}
    };
    etl::list<ScanResultT, 4> scanResult;

    static void wifiTaskTrampoline(void *arg);
    void wifiTask();

    void startAccessPoint();
    void stopAccessPoint();
    /**
     * When Access Point is active, GaTas will keep track of all clients connecting to it
     * It will send regular AccessPointClientsMsg not notify any modules like GDL90
     */
    void handleAccesspointClients();

    static int scanResultCb(void *env, const cyw43_ev_scan_result_t *result);
    bool scanRunning();
    void enableClientMode();
    void disableClientMode();
    void startWifiScan();
    enum ConnectClientResult
    {
        MORE,
        EXHAUSTED,
        CONNECTED
    };
    ConnectClientResult connectClient();
    void stopClient();
    bool checkIfClientActive(int itf);

    void mDnsInit(int itf);
    void mDnsDeinit(int itf);
    void showSsidPwdIp(const etl::string_view &ssid, const etl::string_view &password) const;
    void fillScanResultFromConfiguration();
    void on_receive_unknown(const etl::imessage &msg);
    void publishWifiState() const;
    void on_receive(const GATAS::WifiModeRequestMsg &msg);

    /**
     * Get's the current local IP address
     */
    static WifiService::IpGw getInterfaceInfo();

    virtual bool setData(const etl::string_view data, const etl::string_view path);

public:
    static constexpr const etl::string_view NAME = "WifiService";

    WifiService(etl::imessage_bus &bus, const Configuration &config) : BaseModule(bus, NAME),
                                                                       wifiData(config.wifiService()),
                                                                       connectionState(ConnectionState::START),
                                                                       taskHandle(nullptr),
                                                                       timerHandle(nullptr),
                                                                       totalScanAttempt(NUMBER_OF_CONNECTION_ATTEMPTS),
                                                                       wifiMode(GATAS::WifiMode::NC),
                                                                       requestedWifiMode(GATAS::WifiMode::NC),
                                                                       mdnsSlot(0),
                                                                       wifiStatePublishPending(false),
                                                                       successClientConnected(false),
                                                                       dontScanJustConnectToClient(true)
    {
    }

    // Safe only after failed postConstruct(), before start().
    // Failed CYW43 initialisation has not registered any callbacks to this object.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~WifiService() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;
    virtual void postStart() override;
};
