#pragma once

#include <array>
#include <cassert>
#include "ip_addr.h"
#include "pbuf.h"
#include "pico/cyw43_arch.h"

using err_t = int;
using u16_t = uint16_t;
constexpr err_t ERR_OK = 0;
constexpr err_t ERR_MEM = -1;
constexpr err_t ERR_BUF = -2;
constexpr err_t ERR_ABRT = -3;
constexpr err_t ERR_VAL = -4;
constexpr unsigned TF_ACK_NOW = 1;
constexpr unsigned TF_NODELAY = 2;
constexpr unsigned TCP_WRITE_FLAG_COPY = 1;
constexpr unsigned TCP_WRITE_FLAG_MORE = 2;

struct tcp_pcb
{
    ip_addr_t remote_ip;
    uint16_t remote_port = 2000;
    bool alive = true;
    void *arg = nullptr;
    err_t (*recv)(void *, tcp_pcb *, pbuf *, err_t) = nullptr;
    void (*error)(void *, err_t) = nullptr;
    err_t (*sent)(void *, tcp_pcb *, u16_t) = nullptr;
    err_t (*connected)(void *, tcp_pcb *, err_t) = nullptr;
    err_t (*accept)(void *, tcp_pcb *, err_t) = nullptr;
};

namespace MockTcp
{
    inline std::array<tcp_pcb, 16> pcbs;
    inline size_t allocated = 0;
    inline unsigned writes = 0;
    inline unsigned outputs = 0;
    inline err_t connectResult = ERR_OK;
    inline err_t bindResult = ERR_OK;
    inline bool failAllocation = false;

    inline void check(tcp_pcb *pcb)
    {
        cyw43_arch_lwip_check();
        assert(pcb != nullptr && pcb->alive);
    }

    inline void reset()
    {
        assert(mockLwipLockDepth == 0);
        pcbs = {};
        allocated = writes = outputs = 0;
        connectResult = bindResult = ERR_OK;
        failAllocation = false;
        mockBeforeLwipLock = nullptr;
    }

    inline void disconnect(tcp_pcb &pcb)
    {
        LwipRAIILock lock;
        auto callback = pcb.error;
        pcb.alive = false;
        callback(pcb.arg, ERR_ABRT);
    }
}

inline tcp_pcb *tcp_new_ip_type(unsigned)
{
    cyw43_arch_lwip_check();
    return MockTcp::failAllocation ? nullptr : &MockTcp::pcbs.at(MockTcp::allocated++);
}
inline void tcp_arg(tcp_pcb *pcb, void *arg) { MockTcp::check(pcb); pcb->arg = arg; }
inline void tcp_recv(tcp_pcb *pcb, decltype(tcp_pcb::recv) callback) { MockTcp::check(pcb); pcb->recv = callback; }
inline void tcp_err(tcp_pcb *pcb, decltype(tcp_pcb::error) callback) { MockTcp::check(pcb); pcb->error = callback; }
inline void tcp_sent(tcp_pcb *pcb, decltype(tcp_pcb::sent) callback) { MockTcp::check(pcb); pcb->sent = callback; }
inline void tcp_accept(tcp_pcb *pcb, decltype(tcp_pcb::accept) callback) { MockTcp::check(pcb); pcb->accept = callback; }
inline void tcp_set_flags(tcp_pcb *pcb, unsigned) { MockTcp::check(pcb); }
inline err_t tcp_connect(tcp_pcb *pcb, const ip_addr_t *, uint16_t, decltype(tcp_pcb::connected) callback)
{
    MockTcp::check(pcb);
    pcb->connected = callback;
    return MockTcp::connectResult;
}
inline unsigned tcp_sndbuf(tcp_pcb *pcb) { MockTcp::check(pcb); return 1024; }
inline err_t tcp_write(tcp_pcb *pcb, const void *, size_t, unsigned) { MockTcp::check(pcb); ++MockTcp::writes; return ERR_OK; }
inline err_t tcp_output(tcp_pcb *pcb) { MockTcp::check(pcb); ++MockTcp::outputs; return ERR_OK; }
inline err_t tcp_close(tcp_pcb *pcb) { MockTcp::check(pcb); pcb->alive = false; return ERR_OK; }
inline void tcp_abort(tcp_pcb *pcb) { MockTcp::check(pcb); pcb->alive = false; }
inline void tcp_recved(tcp_pcb *pcb, size_t) { MockTcp::check(pcb); }
inline err_t tcp_bind(tcp_pcb *pcb, const ip_addr_t *, uint16_t) { MockTcp::check(pcb); return MockTcp::bindResult; }
inline tcp_pcb *tcp_listen_with_backlog(tcp_pcb *pcb, unsigned) { MockTcp::check(pcb); return pcb; }
