#include <stdio.h>

#include "../gdloverudp.hpp"

/* GATAS */
#include "ace/coreutils.hpp"
#include "ace/semaphoreguard.hpp"
#include "ace/measure.hpp"
#include "ace/lwipraiilock.hpp"
#include "ace/scopedpbuf.hpp"
#include "ace/spinlockguard.hpp"

/* LwIP */
#include "lwip/ip_addr.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"

/* Vendor */
#include "tiny-json.h"

void GDLoverUDP::getConfiguration(const Configuration &config)
{
    getConfigurationNoMutex(config);
}

void GDLoverUDP::getConfigurationNoMutex(const Configuration &config)
{
    auto guard = SpinlockGuard{CoreUtils::sharedSpinLock()};
    udpPorts.clear();
    customClients.clear();

    etl::string<16> path;
    for (uint8_t i = 0; i < GATAS_GDL90OVERUDP_MAX_PORTS; i++)
    {
        if (!udpPorts.full())
        {
            path.clear();
            etl::string_stream stream(path);
            stream << etl::make_string("defaultPorts/") << i;
            int32_t p = config.valueByPath(GDL90OVERUDP_DEFAULT_PORT, NAME, path);
            udpPorts.insert(p);
        }
    }

    for (char i = 0; i < GATAS_GDL90OVERUDP_MAX_CUSTOM_CLIENTS; i++)
    {
        path.clear();
        etl::string_stream stream(path);
        stream << etl::make_string("ips/") << i;
        auto ipPort = config.ipPortBypath(NAME, path);
        if (ipPort.ip != IPADDR_NONE && !customClients.full())
        {
            // printf("GDLoverUDP %ld:%d\n",ipPort.ip, ipPort.port);
            customClients.emplace_back(ipPort.ip, ipPort.port);
        }
    }
}

GATAS::PostConstruct GDLoverUDP::postConstruct()
{
    {
        LwipRAIILock lock;
        sendPcb = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (sendPcb == nullptr)
        {
            return GATAS::PostConstruct::NETWORK_ERROR;
        }
        foreFlightPCB = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (foreFlightPCB == nullptr)
        {
            udp_remove(sendPcb);
            sendPcb = nullptr;
            return GATAS::PostConstruct::NETWORK_ERROR;
        }

        // https://www.foreflight.com/connect/spec/
        if (udp_bind(foreFlightPCB, IP_ADDR_ANY, FOREFLIGHT_UDP_PORT) != ERR_OK)
        {
            udp_remove(foreFlightPCB);
            udp_remove(sendPcb);
            foreFlightPCB = nullptr;
            sendPcb = nullptr;
            return GATAS::PostConstruct::NETWORK_ERROR;
        }
    }

    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr)
    {
        LwipRAIILock lock;
        udp_remove(foreFlightPCB);
        udp_remove(sendPcb);
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    if (xTaskCreate(gdlOverUDPTask, GDLoverUDP::NAME.cbegin(), configMINIMAL_STACK_SIZE + 256, this, tskIDLE_PRIORITY + 3, &taskHandle) != pdPASS)
    {
        vSemaphoreDelete(mutex);
        LwipRAIILock lock;
        udp_remove(foreFlightPCB);
        udp_remove(sendPcb);
        return GATAS::PostConstruct::TASK_ERROR;
    }
    GATAS_REGISTER_MUTEX(mutex, "GDLoverUDP_mutex");
    {
        LwipRAIILock lock;
        udp_recv(foreFlightPCB, foreFlightListener, this);
    }
    return GATAS::PostConstruct::OK;
}

void GDLoverUDP::foreFlightListener(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *remoteAddr, u16_t port)
{
    (void)arg;
    (void)pcb;
    (void)port;
    if (!p)
    {
        return;
    }
    ScopedPbuf scopedPbuf(p);

    GATAS_MEASURE("foreFlightListener", 90);
    GDLoverUDP *that = static_cast<GDLoverUDP *>(arg);

    // When this FF client has been detected already, then ignore
    if (auto guard = SpinlockGuard{CoreUtils::sharedSpinLock()})
    {
        if (that->connectedClients.contains(ip4_addr_get_u32(ip_2_ip4(remoteAddr))) || that->connectedClients.full())
        {
            return;
        }
    }

    auto buffer = static_cast<char *>(p->payload);
    buffer[p->len] = 0;
    json_t pool[4];
    // GATAS_INFO(buffer);
    json_t const *root = json_create(buffer, pool, 4);
    if (!root)
    {
        return;
    }

    json_t const *gdl = json_getProperty(root, "GDL90");
    if (!gdl)
    {
        return;
    }

    // Extract port
    json_t const *portProp = json_getProperty(gdl, "port");
    if (!portProp)
    {
        return;
    }

    if (json_getType(portProp) != JSON_INTEGER)
    {
        return;
    }

    auto outPort = static_cast<int32_t>(json_getInteger(portProp));
    if (outPort < 1 || outPort > 65535)
    {
        return;
    }

    auto guard = SpinlockGuard{CoreUtils::sharedSpinLock()};
    auto portExists = that->udpPorts.contains(static_cast<uint16_t>(outPort));

    // Only add this new client if we have space for a new port
    // or the existing ports exists and there is room for the address
    that->statistics.foreFlightBroadcasts += 1;

    if (portExists || !that->udpPorts.full())
    {
        // Add the CLient's IP
        that->connectedClients.insert(ip4_addr_get_u32(ip_2_ip4(remoteAddr)));

        // Add the Clients Port
        that->udpPorts.insert(static_cast<uint16_t>(outPort));
    }
}

void GDLoverUDP::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"heartbeatTx:k\":" << statistics.heartbeatTx;
    stream << ",\"sendFailure:err\":" << statistics.sendFailureErr;
    stream << ",\"foreFlightBroadcasts:k\":" << statistics.foreFlightBroadcasts;
    stream << ",\"currentAddressesInUse\":" << connectedClients.size();
    stream << ",\"maxPorts\":" << udpPorts.max_size();
    stream << ",\"currentPortsInUse\":" << udpPorts.size();
    stream << ",\"foreFlightClients\":[";
    bool firstClient = true;
    for (const auto client : connectedClients)
    {
        ip4_addr_t ip(client);
        char ipStr[IP4ADDR_STRLEN_MAX];
        ip4addr_ntoa_r(&ip, ipStr, sizeof(ipStr));

        for (const auto port : udpPorts)
        {
            if (!firstClient)
            {
                stream << ",";
            }
            stream << "{\"ip\":\"" << ipStr << "\",\"port\":" << port << "}";
            firstClient = false;
        }
    }
    stream << "]";
    stream << "}";
}

void GDLoverUDP::start()
{
    getBus().subscribe(*this);
};

void GDLoverUDP::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void GDLoverUDP::on_receive(const GATAS::AccessPointClientsMsg &msg)
{
    connectedClients = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), msg.msg);
}

void GDLoverUDP::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == GDLoverUDP::NAME)
    {
        getConfiguration(msg.config);
    }
}

void GDLoverUDP::gdlOverUDPTask(void *arg)
{
    GDLoverUDP *at = static_cast<GDLoverUDP *>(arg);
    while (true)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, TASK_DELAY_MS(1000));

        // Handle TRANSMIT
        if (notifyValue == 0 || (notifyValue & TaskState::TRANSMIT))
        {
            at->transmitBuffer();
        }
    }
}

void GDLoverUDP::on_receive(const GATAS::GdlMsg &msg)
{
    if (auto guard = SemaphoreGuard(10, mutex))
    {
        gdlDataBuffer.set(msg.msg);

        // When the buffer is nearly full 3/4, request for immediate send
        if (gdlDataBuffer.packets() >= (NUM_GDL_PACKETS - NUM_GDL_PACKETS / 4))
        {
            xTaskNotify(taskHandle, TaskState::TRANSMIT, eSetBits);
        }
    }
}

void GDLoverUDP::transmitBuffer()
{
    // When no network, don't send any UDP packages
    if (!wifiConnected)
    {
        return;
    }

    auto [connectedClientsSnapshot, udpPortsSnapshot] =
        SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), connectedClients, udpPorts);

    uint8_t totalpBufs = connectedClientsSnapshot.size() * udpPortsSnapshot.size() +
                         (gateWayClient ? udpPortsSnapshot.size() : 0);
    for (const auto &client : customClients)
    {
        if ((client.ip & 0xFFFFFF) == networkAddress)
        {
            totalpBufs += 1;
        }
    }
    if (totalpBufs == 0)
    {
        return;
    }

    etl::optional<etl::span<uint8_t>> part;
    if (auto guard = SemaphoreGuard(1000, mutex))
    {
        gdlDataBuffer.compact();
        part = gdlDataBuffer.read();
    }
    if (!part)
    {
        return;
    }

    auto data = part.value();
    LwipRAIILock lock;

    // Send to the connect clients and the defined ports
    for (auto ip : connectedClientsSnapshot)
    {
        // Connected clients are always on the accesspoint,
        // thus we don't need to test for the networkAddress
        for (auto port : udpPortsSnapshot)
        {
            sendTo(ip, port, data);
        }
    }

    // Send to the gateway client, which is most lickly running a EFB
    if (gateWayClient)
    {
        for (auto port : udpPortsSnapshot)
        {
            sendTo(gateWayClient, port, data);
        }
    }

    // Custom clients can have any netmask and thus need to be tested if they are on the local net
    for (const auto &client : customClients)
    {
        if ((client.ip & 0xFFFFFF) == networkAddress)
        {
            sendTo(client.ip, client.port == 0 ? GDL90OVERUDP_DEFAULT_PORT : client.port, data);
        }
    }
}

void GDLoverUDP::sendTo(uint32_t ip, uint16_t port, etl::span<uint8_t> data)
{
    // transmitBuffer owns the core lock for the complete send batch.
    cyw43_arch_lwip_check();
    ip_addr_t addr;
    ip4_addr_set_u32(&addr, ip);

    struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, data.size(), PBUF_POOL);

    if (!pbuf)
    {
        return;
    }
    if (pbuf_take(pbuf, data.begin(), data.size()) != ERR_OK)
    {
        pbuf_free(pbuf);
        return;
    }
    err_t err = udp_sendto(sendPcb, pbuf, &addr, port);
    pbuf_free(pbuf);

    if (err == ERR_OK)
    {
        statistics.heartbeatTx += 1;
    }
    else
    {
        statistics.sendFailureErr += 1;
    }
}

void GDLoverUDP::on_receive(const GATAS::WifiConnectionStateMsg &msg)
{
    wifiConnected = msg.wifiMode != GATAS::WifiMode::NC;
    networkAddress = msg.gatasIp & 0xFFFFFF;
    if (msg.wifiMode == GATAS::WifiMode::CLIENT)
    {
        gateWayClient = msg.gateWay;
    }
    else
    {
        gateWayClient = 0;
    }
}
