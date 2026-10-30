#include <stdio.h>
#include <string.h>

#include "../gatasconnect.hpp"
#include "../gatasconnectudp.hpp"
#include "ace/coreutils.hpp"
#include "ace/debug.hpp"
#include "ace/lwipraiilock.hpp"
#include "ace/scopedpbuf.hpp"

#include "etl/array.h"
#include "etl/algorithm.h"

/* LwIP */
#include "lwip/ip_addr.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/dns.h"

GATAS::PostConstruct GatasConnectUDP::postConstruct()
{
    LwipRAIILock lock;
    pcbSend = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcbSend == nullptr)
    {
        return GATAS::PostConstruct::NETWORK_ERROR;
    }
    udp_recv(pcbSend, receiveUdpMessage, this);

    return GATAS::PostConstruct::OK;
}

void GatasConnectUDP::start()
{
    getBus().subscribe(*this);
}

void GatasConnectUDP::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"bytesReceived:kb\":" << statistics.bytesReceived;
    stream << ",\"bytesSend:kb\":" << statistics.bytesSend;
    stream << ",\"pkgReceived:k\":" << statistics.pkgReceived;
    stream << ",\"pkgSend:k\":" << statistics.pkgSend;
    stream << ",\"pBufferAlloc:err\":" << statistics.pBufferAllocErr;
    stream << ",\"msgSendFailed:err\":" << statistics.msgSendFailed;
    stream << ",\"hasConnection:b\":" << statistics.hasConnection;
    stream << "}";
}

void GatasConnectUDP::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void GatasConnectUDP::on_receive(const GATAS::WifiConnectionStateMsg &wcs)
{
    wifiConnected = wcs.wifiMode != GATAS::WifiMode::NC;
    gatasIp = wcs.gatasIp;
}

void GatasConnectUDP::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == GatasConnectUDP::NAME || msg.moduleName == Configuration::NAME)
    {
        getConfig(msg.config);
    }
}

void GatasConnectUDP::getConfig(const Configuration &config)
{
    gatasServerStr = config.strValueByPath("gatas.vantwisk.nl", GatasConnectUDP::NAME, "gatasServer/ip");
    gatasServerIPAddress = IPADDR4_INIT(IPADDR_NONE);
}

void GatasConnectUDP::resolveGatasServerCallback(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    auto *ctx = static_cast<GatasConnectUDP *>(arg);
    if (ipaddr != nullptr && ipaddr->addr != IPADDR_NONE)
    {
        ctx->gatasServerIPAddress = *ipaddr;
        GATAS_INFO("Resolved GATAS server %s -> %u.%u.%u.%u", name, ip4_addr1(ipaddr), ip4_addr2(ipaddr), ip4_addr3(ipaddr), ip4_addr4(ipaddr));
    }
    else
    {
        GATAS_WARN("Failed to resolve GATAS server %s, will retry", name);
    }
}

etl::optional<ip_addr_t> GatasConnectUDP::resolveIP()
{
    LwipRAIILock lock;
    if (gatasServerIPAddress.addr == IPADDR_NONE)
    {
        err_t err = dns_gethostbyname(gatasServerStr.c_str(), &gatasServerIPAddress, resolveGatasServerCallback, this);
        if (err != ERR_INPROGRESS && err != ERR_OK)
        {
            GATAS_WARN("dns_gethostbyname failed for %s, retrying", gatasServerStr.c_str());
        }
    }
    if (gatasServerIPAddress.addr != IPADDR_NONE && !ip_addr_isloopback(&gatasServerIPAddress) && !ip_addr_isany(&gatasServerIPAddress))
    {
        return gatasServerIPAddress;
    }
    return etl::nullopt;
}

void GatasConnectUDP::on_receive(const GATAS::GatasConnectTx &msg)
{
    if (!msg.output.usesUDP())
    {
        return;
    }

    if (!wifiConnected)
    {
        return;
    }
    const auto serverAddress = resolveIP();
    if (!serverAddress)
    {
        return;
    }

    size_t sendLength = msg.length;
    etl::array<uint8_t, ANDROIDHOTSPOT_FIX_HIGHMARK> sendBuffer{};
    const uint8_t *payload = msg.cobsMessage.get();

    if (ANDROIDHOTSPOT_FIX)
    {
        lastSendCounter += 1;
        size_t fillSize = ANDROIDHOTSPOT_FIX_LOWMARK;
        if (lastSendCounter > ANDROIDHOTSPOT_COUNT_UNTILL_HIGH)
        {
            lastSendCounter = ANDROIDHOTSPOT_COUNT_UNTILL_HIGH;
            fillSize = ANDROIDHOTSPOT_FIX_HIGHMARK;
        }

        if (fillSize > sendLength)
        {
            etl::copy(msg.cobsMessage.get(), msg.cobsMessage.get() + msg.length, sendBuffer.begin());
            payload = sendBuffer.data();
            sendLength = fillSize;
        }
    }

    struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, sendLength, PBUF_POOL);
    if (pbuf == nullptr)
    {
        statistics.pBufferAllocErr++;
        return;
    }

    if (pbuf_take(pbuf, payload, sendLength) != ERR_OK)
    {
        pbuf_free(pbuf);
        return;
    }

    err_t err;
    {
        LwipRAIILock lock;
        err = udp_sendto(pcbSend, pbuf, &serverAddress.value(), GATAS_CONNECT_PORT);
    }
    pbuf_free(pbuf);
    if (err == ERR_OK)
    {
        statistics.hasConnection = true;
        statistics.pkgSend += 1;
        statistics.bytesSend += sendLength;
    }
    else
    {
        statistics.msgSendFailed += 1;
        statistics.hasConnection = false;
    }
}

void GatasConnectUDP::receiveUdpMessage(void *arg, struct udp_pcb *pcb,
                                        struct pbuf *pbuf, const ip_addr_t *addr, u16_t port)
{
    (void)pcb;
    (void)addr;
    (void)port;
    auto *taskCtx = static_cast<GatasConnectUDP *>(arg);

    if (pbuf == nullptr)
    {
        return;
    }
    ScopedPbuf scopedPbuf(pbuf);

    auto *copy = static_cast<uint8_t *>(BaseModule::getGlobalPool().alloc(pbuf->tot_len));
    if (copy == nullptr)
    {
        taskCtx->statistics.pBufferAllocErr += 1;
        return;
    }

    // Receiving traffic means the hotspot/Android path path is healthy again, so restart the
    // Android small-packet workaround cycle from the low watermark.
    taskCtx->lastSendCounter = 0;

    size_t pos = 0;
    for (struct pbuf *q = pbuf; q != nullptr; q = q->next)
    {
        memcpy(copy + pos, q->payload, q->len);
        pos += q->len;
    }

    taskCtx->statistics.bytesReceived += pbuf->tot_len;
    taskCtx->statistics.pkgReceived += 1;
    taskCtx->statistics.hasConnection = true;
    taskCtx->getBus().receive(GATAS::GatasConnectRx(BaseModule::getGlobalPool(), GATAS::GatasConnectTransport::UDP, copy, pbuf->tot_len));
}
