#pragma once

#include "ace/models.hpp"
#include "etl/list.h"
#include "etl/set.h"

// A short, coherent snapshot of the existing UDP output policy.
struct Gdl90UdpRoutes
{
    static constexpr uint16_t DEFAULT_PORT = 4000;
    bool connected = false;
    uint32_t network = 0;
    uint32_t gateway = 0;
    etl::set<uint32_t, GATAS_MAXIMUM_TCP_CLIENTS> clients;
    etl::set<uint16_t, GATAS_GDL90OVERUDP_MAX_PORTS> ports;
    etl::list<GATAS::Config::IpPort, GATAS_GDL90OVERUDP_MAX_CUSTOM_CLIENTS> custom;

    template<typename Callback>
    void forEachDestination(Callback callback) const
    {
        if (!connected)
        {
            return;
        }
        for (const auto ip : clients)
        {
            for (const auto port : ports)
            {
                callback(ip, port);
            }
        }
        if (gateway != 0)
        {
            for (const auto port : ports)
            {
                callback(gateway, port);
            }
        }
        for (const auto &client : custom)
        {
            if ((client.ip & 0xFFFFFF) == network)
            {
                callback(client.ip, client.port == 0 ? DEFAULT_PORT : client.port);
            }
        }
    }

    uint16_t recipientCount() const
    {
        // Count distinct IP:port targets, even if several output rules overlap.
        constexpr size_t MAX_TARGETS = (GATAS_MAXIMUM_TCP_CLIENTS + 1) * GATAS_GDL90OVERUDP_MAX_PORTS +
            GATAS_GDL90OVERUDP_MAX_CUSTOM_CLIENTS;
        etl::set<uint64_t, MAX_TARGETS> targets;
        forEachDestination([&targets](uint32_t ip, uint16_t port)
        {
            targets.insert((static_cast<uint64_t>(ip) << 16) | port);
        });
        return targets.size();
    }
};
