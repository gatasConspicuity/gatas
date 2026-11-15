#pragma once

#include <stdint.h>

#include "ace/tcpclient.hpp"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* LwIP */
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"

/* ETLCPP */
#include "etl/list.h"
#include "etl/string.h"

/* GaTas */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/packetbuffer.hpp"

/**
 * AirConnect protocol for EFB's that only support AirConnect.
 * THis protocol is currently not recommende if you can also use GDL90, if you need to use NMEA and have BLE, use that
 *
 * READ THIS: THis module needs more testing, I have tested it a long time ago... it worked... but made changes to the buffers later on without testing as I used bluetooth and GDL90
 */
class AirConnect : public BaseModule, public etl::message_router<AirConnect, GATAS::DataPortMsg, GATAS::WifiConnectionStateMsg, GATAS::Every5SecMsg>
{
    friend class message_router;
    static constexpr uint16_t AIRCONNECT_PORT = 2000;
    struct
    {
        uint32_t toManyClients = 0;
        uint16_t tcpWriteErr = 0;
        uint32_t newClientConnection = 0;
    } statistics;

    struct TcpClientState
    {
        static constexpr uint16_t PACKET_BUFFER_SIZE = 512;
        PacketBuffer<PACKET_BUFFER_SIZE, PACKET_BUFFER_SIZE / (GATAS::NMEA_MAX_LENGTH / 2)> buffer;
        AirConnect *airConnect = nullptr;
        uint16_t bufferOverrunErr = 0;
        TcpClient tcpClient;

        bool isDisconnected() const
        {
            return tcpClient.isDisconnected();
        }
        void write(const etl::string_view view, bool flush = false)
        {
            buffer.setString(view);
            if (buffer.used() > 250 || flush)
            {
                auto oData = buffer.read();
                if (oData)
                {
                    auto data = oData.value();
                    std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());
                    tcpClient.write(data);
                    buffer.compact();
                }
            }
        }

        void tcpReceiveHandler(etl::span<uint8_t> data)
        {
            (void)data;
        }

        TcpClientState() = delete;
        TcpClientState(struct tcp_pcb *pcb_, AirConnect *airConnect_) : airConnect(airConnect_),
                                                                        bufferOverrunErr(0),
                                                                        tcpClient(pcb_, TcpClient::ReceiveHandler::create<TcpClientState, &TcpClientState::tcpReceiveHandler>(*this))
        {
            (void)pcb_;
        }

        TcpClientState(const TcpClientState &) = delete;
        TcpClientState &operator=(const TcpClientState &) = delete;
        TcpClientState(TcpClientState &&) = delete;
        TcpClientState &operator=(TcpClientState &&) = delete;
    };

    using ConnectedClients = etl::list<TcpClientState, GATAS_MAXIMUM_TCP_CLIENTS>;
    ConnectedClients connectedClients;
    bool wifiConnected;
    SemaphoreHandle_t mutex;
    TcpListener tcpListener;

private:
    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    void on_receive(const GATAS::DataPortMsg &msg);

    void on_receive(const GATAS::WifiConnectionStateMsg &wcs);

    void on_receive(const GATAS::Every5SecMsg &msg);

    void on_receive_unknown(const etl::imessage &msg);

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
    err_t tcpAcceptHandler(struct tcp_pcb *pcb);

public:
    static constexpr const char *NAME = "AirConnect";
    AirConnect(etl::imessage_bus &bus, const Configuration &config) : BaseModule(bus, NAME), wifiConnected(false), mutex(nullptr),
                                                                      tcpListener(AIRCONNECT_PORT, TcpListener::AcceptHandler::create<AirConnect, &AirConnect::tcpAcceptHandler>(*this))
    {
        (void)config;
    }

    virtual ~AirConnect() = default;
};
