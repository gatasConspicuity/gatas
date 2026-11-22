#pragma once

/* System. */
#include <stdint.h>

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/tcpbufferedclient.hpp"

/**
 * Converts dump1090 TCP lines into binary Mode S bus messages.
 * Note: This module is experimental
 */
class Dump1090Client : public BaseModule,
                       public etl::message_router<Dump1090Client, GATAS::WifiConnectionStateMsg>
{
    friend class message_router;

    struct
    {
        uint32_t totalReceived = 0;
        uint32_t poolDrops = 0;
        uint32_t reConnects = 0;
    } statistics;

    bool wifiConnected = false;
    uint32_t networkAddress = 0;
    using TcpClient = TcpBufferedClient<48, 10>;
    TcpClient tcpClient;

public:
    void on_receive(const GATAS::WifiConnectionStateMsg &wcs);
    void on_receive_unknown(const etl::imessage &msg);

public:
    static constexpr const char *NAME = "Dump1090Client";

    Dump1090Client(etl::imessage_bus &bus, const Configuration &config) : BaseModule(bus, NAME),
                                                                          tcpClient(
                                                                              config.ipPortBypath(NAME),
                                                                              DelimiterBitmap::CRLF(),
                                                                              StreamLineParser<TcpClient>::CallBackFunction::create<Dump1090Client, &Dump1090Client::processNewSentence>(*this))

    {
        tcpClient.autoConnect(true);
    }

    // Safe only after failed postConstruct(), before start().
    // The TCP client is not started until start(); failure retains no external callbacks.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~Dump1090Client() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    void processNewSentence(etl::span<uint8_t> data);
};
