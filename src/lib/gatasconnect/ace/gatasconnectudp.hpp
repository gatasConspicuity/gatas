#pragma once

#include <stdint.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "timers.h"

/* LwIP */
#include "lwip/udp.h"
#include "etl/optional.h"

/* GaTas */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"

/**
 * UDP transport for GatasConnect. This module only sends and receives raw
 * COBS payloads. It does not know anything about framing or the binary
 * payload format.
 */
class GatasConnectUDP : public BaseModule, public etl::message_router<GatasConnectUDP, GATAS::WifiConnectionStateMsg, GATAS::ConfigUpdatedMsg, GATAS::GatasConnectTx>
{
    friend class message_router;
    // THis is used to fix a android HotSpot issue where small packages are not routed.
    // It sends a large and a small package size to ensure that Android sees them as important
    static constexpr size_t ANDROIDHOTSPOT_FIX_HIGHMARK = 128 * 3;
    static constexpr size_t ANDROIDHOTSPOT_FIX_LOWMARK = 160;
    static constexpr bool ANDROIDHOTSPOT_FIX = true;
    // After how many 'sends' the system sends larger packages again
    static constexpr uint8_t ANDROIDHOTSPOT_COUNT_UNTILL_HIGH = 5;

    struct
    {
        uint32_t bytesReceived = 0;
        uint32_t bytesSend = 0;
        uint32_t pkgReceived = 0;
        uint32_t pkgSend = 0;
        uint32_t pBufferAllocErr = 0;
        uint32_t msgSendFailed = 0;
        bool hasConnection = false;
    } statistics;

    bool wifiConnected = false;
    uint32_t gatasIp = 0;
    uint8_t lastSendCounter = 0;

    udp_pcb *pcbSend = nullptr;
    ip_addr_t gatasServerIPAddress = IPADDR4_INIT(IPADDR_NONE);
    GATAS::ConfigString gatasServerStr;

private:
    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    void on_receive(const GATAS::WifiConnectionStateMsg &wcs);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::GatasConnectTx &msg);
    void on_receive_unknown(const etl::imessage &msg);

    void getConfig(const Configuration &config);
    etl::optional<ip_addr_t> resolveIP();
    static void resolveGatasServerCallback(const char *name, const ip_addr_t *ipaddr, void *arg);
    static void receiveUdpMessage(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);

public:
    static constexpr const char *NAME = "GatasConnectUDP";
    GatasConnectUDP(etl::imessage_bus &bus, Configuration &config) : BaseModule(bus, NAME)
    {
        getConfig(config);
    }

    virtual ~GatasConnectUDP() = default;
};
