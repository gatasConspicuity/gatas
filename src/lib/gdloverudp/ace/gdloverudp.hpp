#pragma once

#include <stdint.h>
#include <sys/time.h>

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/packetbuffer.hpp"

/* LwIP */
#include "lwip/udp.h"

/* ETL CPP */
#include "etl/message_bus.h"
#include "etl/set.h"
#include "etl/list.h"

/**
 * Client that can connect to a host and a port and expect to receive line terminated NMEA Messages
 * TODO: de-Couple UDP from the GDL90 so this service send GDL Message over the messagebus which can then be send over UDP or Serial or BlueToolh
 */
class GDLoverUDP : public BaseModule, public etl::message_router<GDLoverUDP, GATAS::GdlMsg, GATAS::AccessPointClientsMsg, GATAS::ConfigUpdatedMsg, GATAS::WifiConnectionStateMsg>
{
    friend class message_router;

    static constexpr uint16_t GDL90OVERUDP_DEFAULT_PORT = 4000;                                 // Default port
    static constexpr uint16_t NUM_GDL_PACKETS = static_cast<int>(512 / sizeof(GATAS::GDLData)); // Number of GDL packets that can be buffered
    static constexpr uint16_t UDP_BUFFER_SIZE = NUM_GDL_PACKETS * sizeof(GATAS::GDLData);
    static constexpr uint32_t FOREFLIGHT_UDP_PORT = 63093;
    enum TaskState : uint32_t
    {
        TRANSMIT = 1 << 2,
    };

    mutable struct
    {
        uint32_t heartbeatTx = 0;
        uint32_t sendFailureErr = 0;
        uint32_t foreFlightBroadcasts = 0;
    } statistics;

    udp_pcb *sendPcb;
    udp_pcb *foreFlightPCB;
    uint32_t networkAddress;
    TaskHandle_t taskHandle;
    SemaphoreHandle_t mutex;
    uint32_t gateWayClient;
    bool wifiConnected;

    etl::list<GATAS::Config::IpPort, GATAS_GDL90OVERUDP_MAX_CUSTOM_CLIENTS> customClients;
    etl::set<uint32_t, GATAS_MAXIMUM_TCP_CLIENTS> connectedClients;
    etl::set<uint16_t, GATAS_GDL90OVERUDP_MAX_PORTS> udpPorts = {};
    PacketBuffer<UDP_BUFFER_SIZE, NUM_GDL_PACKETS> gdlDataBuffer;

private:
    void getConfiguration(const Configuration &config);
    void getConfigurationNoMutex(const Configuration &config);
    void on_receive_unknown(const etl::imessage &msg);
    void on_receive(const GATAS::AccessPointClientsMsg &msg);
    void on_receive(const GATAS::WifiConnectionStateMsg &msg);
    void on_receive(const GATAS::GdlMsg &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    static void gdlOverUDPTask(void *arg);
    static void foreFlightListener(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, u16_t port);
    void transmitBuffer();
    void sendTo(uint32_t ip, uint16_t port, etl::span<uint8_t> data);

public:
    static constexpr const etl::string_view NAME = "GDLoverUDP";
    GDLoverUDP(etl::imessage_bus &bus, const Configuration &config) : BaseModule(bus, NAME),
                                                                      sendPcb(nullptr),
                                                                      foreFlightPCB(nullptr),
                                                                      networkAddress(0),
                                                                      taskHandle(nullptr),
                                                                      mutex(nullptr),
                                                                      gateWayClient{0},
                                                                      wifiConnected(false) {
        getConfigurationNoMutex(config);
}

virtual ~GDLoverUDP() = default;

virtual GATAS::PostConstruct postConstruct() override;

virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

virtual void start() override;
}
;
