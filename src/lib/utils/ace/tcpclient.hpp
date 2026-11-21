#pragma once

#include <stdint.h>
#include "ace/models.hpp"
#include "ace/lwipraiilock.hpp"
#include "ace/debug.hpp"

#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "etl/delegate.h"
#include "etl/span.h"

class TcpClient
{
#define TCP_LOG(fmt, ...) GATAS_LOG_IF(0, fmt, ##__VA_ARGS__)

friend class TcpListener;

public:
    using ReceiveHandler = etl::delegate<void(etl::span<uint8_t>)>;
    using SentHandler = etl::delegate<void(uint16_t)>;
    using ConnectHandler = etl::delegate<void(bool)>; // optional

    enum class State
    {
        Disconnected,
        Connecting,
        Connected
    };

private:
    constexpr static int8_t DEFAULT_NUM_AUTOCLOSE = 8;
    struct tcp_pcb *pcb;
    State state;

    GATAS::Config::IpPort ipPort;
    // When in write it's noticed that the closed is closed, it will be automatically be re-opened
    bool autoReConnect;
    // Counter that closes the connection when reaches zero after failed writes. When 0, the conneciton is closed
    int8_t closeConnectionCnt;
    ReceiveHandler onReceive;
    SentHandler onSent;
    ConnectHandler onConnect; // optional (can be empty)

public:
    TcpClient(GATAS::Config::IpPort ipPort_,
              ReceiveHandler onReceive_,
              SentHandler onSent_,
              ConnectHandler onConnect_ = ConnectHandler())
        : pcb(nullptr),
          state(State::Disconnected),
          ipPort(ipPort_),
          autoReConnect(false),
          closeConnectionCnt(DEFAULT_NUM_AUTOCLOSE),
          onReceive(onReceive_),
          onSent(onSent_),
          onConnect(onConnect_)
    {
    }

    TcpClient(struct tcp_pcb *existing, ReceiveHandler onReceive_)
        : pcb(nullptr), state(State::Disconnected), ipPort(GATAS::Config::IpPort{existing->remote_ip.addr, existing->remote_port}), autoReConnect(false), closeConnectionCnt(DEFAULT_NUM_AUTOCLOSE), onReceive(onReceive_)
    {
        adopt(existing);
    }

    ~TcpClient() = default;

    void autoConnect(bool v)
    {
        autoReConnect = v;
    }

    bool start()
    {
        LwipRAIILock lock;
        if (state != State::Disconnected)
        {
            return false;
        }

        ip_addr_t remote_addr = IPADDR4_INIT(ipPort.ip);
        pcb = tcp_new_ip_type(IP_GET_TYPE(&remote_addr));
        if (!pcb)
        {
            TCP_LOG("failed to create PCB\n");
            return false;
        }

        state = State::Connecting;
        closeConnectionCnt = DEFAULT_NUM_AUTOCLOSE;

        tcp_arg(pcb, this);
        tcp_recv(pcb, tcp_client_recv);
        tcp_err(pcb, tcp_client_err);
        tcp_sent(pcb, tcp_client_sent);

        err_t err = tcp_connect(pcb, &remote_addr, ipPort.port, tcp_client_connected);
        if (err != ERR_OK)
        {
            TCP_LOG("connect failed (%d)", err);
            tcp_client_close(this);
            return false;
        }
        tcp_set_flags(pcb, TF_ACK_NOW | TF_NODELAY);

        return true;
    }

    void stop()
    {
        tcp_client_close(this);
    }

    bool isConnected() const
    {
        LwipRAIILock lock;
        return state == State::Connected && pcb != nullptr;
    }

    bool isDisconnected() const
    {
        return state == State::Disconnected;
    }

    void reconnect()
    {
        start();
    }

    bool write(etl::span<const uint8_t> data, bool flush = false)
    {
        LwipRAIILock lock;
        if (state != State::Connected || !pcb)
        {
            if (autoReConnect && state == State::Disconnected)
            {
                reconnect();
            }
            return false;
        }

        if (tcp_sndbuf(pcb) < data.size())
        {
            TCP_LOG("Sendbuffer too small %u needed %u", tcp_sndbuf(pcb), data.size());
            return false;
        }

        err_t err = tcp_write(pcb, data.data(), data.size(),
                              TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);

        if (flush || err != ERR_OK)
        {
            tcp_output(pcb);
        }
        if (err == ERR_OK)
        {
            return true;
        }

        if (err == ERR_MEM || err == ERR_BUF || err == ERR_ABRT)
        {
            if (--closeConnectionCnt <= 0)
            {
                tcp_client_close(this);
            }
        }

        TCP_LOG("Failed to write %d c:%d", err, closeConnectionCnt);
        return false;
    }

    bool flush()
    {
        LwipRAIILock lock;
        return state == State::Connected && pcb != nullptr && tcp_output(pcb) == ERR_OK;
    }

    uint32_t ip() const { return ipPort.ip; }

    void adopt(struct tcp_pcb *existing)
    {
        LwipRAIILock lock;
        if (pcb)
        {
            stop();
        }
        pcb = existing;

        tcp_arg(pcb, this);
        tcp_recv(pcb, tcp_client_recv);
        tcp_err(pcb, tcp_client_err);
        tcp_sent(pcb, tcp_client_sent);
        tcp_set_flags(pcb, TF_ACK_NOW | TF_NODELAY);
        state = State::Connected;
    }

    // Setters for the handlers
    void setReceiveHandler(ReceiveHandler handler)
    {
        onReceive = handler;
    }

    void setSentHandler(SentHandler handler)
    {
        onSent = handler;
    }

    void setConnectHandler(ConnectHandler handler)
    {
        onConnect = handler;
    }

private:
    static void tcp_client_close(void *arg)
    {
        TcpClient *self = static_cast<TcpClient *>(arg);
        LwipRAIILock lock;
        if (!self || !self->pcb)
        {
            return;
        }

        tcp_arg(self->pcb, nullptr);
        tcp_recv(self->pcb, nullptr);
        tcp_err(self->pcb, nullptr);
        tcp_sent(self->pcb, nullptr);

        err_t err = tcp_close(self->pcb);
        if (err != ERR_OK)
        {
            TCP_LOG("close failed (%d), aborting", err);
            tcp_abort(self->pcb);
        }

        self->pcb = nullptr;
        self->state = State::Disconnected;
        if (self->onConnect.is_valid())
        {
            self->onConnect(false);
        }
    }

    static void tcp_client_err(void *arg, err_t err)
    {
        (void)err;
        TcpClient *self = static_cast<TcpClient *>(arg);

        TCP_LOG("error callback (%d)", err);

        self->pcb = nullptr;
        self->state = State::Disconnected;
        if (self->onConnect.is_valid())
        {
            self->onConnect(false);
        }
    }

    static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
    {
        TcpClient *self = static_cast<TcpClient *>(arg);

        if (err != ERR_OK)
        {
            TCP_LOG("connect failed (%d)", err);
            tcp_client_close(arg);
            if (self->onConnect.is_valid())
            {
                self->onConnect(false);
            }
            return err;
        }

        self->pcb = tpcb;
        self->state = State::Connected;
        if (self->onConnect.is_valid())
        {
            self->onConnect(true);
        }

        return ERR_OK;
    }

    static err_t tcp_client_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
    {
        (void)pcb;
        TcpClient *self = static_cast<TcpClient *>(arg);
        if (self->onSent.is_valid())
        {
            self->onSent(len);
        }
        return ERR_OK;
    }

    static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pBuf, err_t err)
    {
        TcpClient *self = static_cast<TcpClient *>(arg);
        if (err != ERR_OK || !pBuf)
        {
            tcp_client_close(arg);
            TCP_LOG("client closed during receive");
            return ERR_OK;
        }

        cyw43_arch_lwip_check();

        size_t total = 0;
        for (pbuf *q = pBuf; q != nullptr; q = q->next)
        {
            auto *data = reinterpret_cast<uint8_t *>(q->payload);
            size_t len = q->len;
            if (self->onReceive.is_valid())
            {
                self->onReceive({data, len});
            }
            total += len;
        }

        tcp_recved(tpcb, total);
        pbuf_free(pBuf);

        return ERR_OK;
    }
};

class TcpListener
{
public:
    using AcceptHandler = etl::delegate<err_t(struct tcp_pcb *pcb)>;

private:
    struct tcp_pcb *listenPcb;
    uint16_t port;
    AcceptHandler onAccept;

public:
    explicit TcpListener(uint16_t port_, AcceptHandler onAccept_)
        : listenPcb(nullptr), port(port_), onAccept(onAccept_) {}

    ~TcpListener() { stop(); }

    TcpListener(const TcpListener &) = delete;
    TcpListener &operator=(const TcpListener &) = delete;
    TcpListener(TcpListener &&) = delete;
    TcpListener &operator=(TcpListener &&) = delete;

    bool start()
    {
        LwipRAIILock lock;
        if (listenPcb != nullptr)
        {
            return true;
        }

        listenPcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (!listenPcb)
        {
            TCP_LOG("failed to create PCB");
            return false;
        }

        err_t err = tcp_bind(listenPcb, IP_ANY_TYPE, port);
        if (err != ERR_OK)
        {
            TCP_LOG("bind failed (%d)", err);
            tcp_abort(listenPcb);
            listenPcb = nullptr;
            return false;
        }

        struct tcp_pcb *newpcb = tcp_listen_with_backlog(listenPcb, 4);
        if (!newpcb)
        {
            TCP_LOG("listen failed");
            tcp_abort(listenPcb);
            listenPcb = nullptr;
            return false;
        }

        listenPcb = newpcb;
        tcp_arg(listenPcb, this);
        tcp_accept(listenPcb, &TcpListener::on_accept_static);

        TCP_LOG("listening on port %u\n", port);
        return true;
    }

    void stop()
    {
        LwipRAIILock lock;
        if (!listenPcb)
        {
            return;
        }

        err_t err = tcp_close(listenPcb);
        if (err != ERR_OK)
        {
            tcp_abort(listenPcb);
        }

        listenPcb = nullptr;
    }

private:
    static err_t on_accept_static(void *arg, struct tcp_pcb *new_pcb, err_t err)
    {
        TcpListener *self = static_cast<TcpListener *>(arg);
        if (!self || err != ERR_OK || !new_pcb)
        {
            return ERR_VAL;
        }

        if (self->onAccept.is_valid())
        {
            return self->onAccept(new_pcb);
        }

        return ERR_VAL;
    }
};
