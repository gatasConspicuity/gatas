#include <catch2/catch_test_macros.hpp>
#include "../../bluetooth/ace/cobstxstatistics.hpp"
#include "../../gdloverudp/ace/gdl90udproutes.hpp"
#include "ace/circularbuffer.hpp"

TEST_CASE("COBS sends count completed frames across BLE fragmentation and retries", "[bluetooth][monitor]")
{
    CobsTxStatistics stats;
    const uint8_t first[] = {2, 1};
    const uint8_t middle[] = {1, 0, 3, 4};
    const uint8_t last[] = {0, 1, 0};
    stats.record(first, true);
    REQUIRE(stats.packets == 0);
    REQUIRE(stats.bytes == 2);
    stats.record(middle, false);
    REQUIRE(stats.packets == 0);
    REQUIRE(stats.bytes == 2);
    REQUIRE(stats.notifications == 1);
    REQUIRE(stats.errors == 1);
    stats.record(middle, true);
    REQUIRE(stats.packets == 1);
    stats.record(last, true);
    REQUIRE(stats.packets == 3);
    REQUIRE(stats.bytes == 9);
    REQUIRE(stats.notifications == 3);
    REQUIRE(stats.errors == 1);
}

TEST_CASE("COBS terminators are counted correctly across a circular-buffer wrap", "[bluetooth][monitor]")
{
    CircularBuffer<8> buffer;
    CobsTxStatistics stats;
    const char prefix[] = {1, 2, 3, 4, 5, 0};
    REQUIRE(buffer.push(prefix, sizeof(prefix)));
    auto send = [&]()
    {
        const auto part = buffer.peek();
        stats.record({reinterpret_cast<const uint8_t *>(part.part), part.size}, true);
        buffer.accepted(part.size);
    };
    send();
    const char wrapped[] = {2, 7, 0, 1, 0};
    REQUIRE(buffer.push(wrapped, sizeof(wrapped)));
    send();
    REQUIRE(stats.packets == 1);
    send();
    REQUIRE(stats.packets == 3);
    REQUIRE(stats.bytes == 11);
    REQUIRE(stats.notifications == 3);
    REQUIRE(buffer.empty());
}

TEST_CASE("UDP monitoring follows the actual output policy and deduplicates destinations", "[udp][monitor]")
{
    Gdl90UdpRoutes routes;
    // IPv4 values use lwIP's byte order, with the host octet in the high byte.
    routes.network = 0x000200C0; // 192.0.2.0/24, documentation-only addresses.
    const uint32_t client = 0x010200C0;
    routes.clients.insert(client);
    routes.ports.insert(4000);
    routes.ports.insert(49002);
    routes.gateway = client;
    routes.custom.push_back({client, 0});
    routes.custom.push_back({0x016433C6, 4000}); // 198.51.100.1, outside the subnet.
    REQUIRE(routes.recipientCount() == 0);
    routes.connected = true;
    REQUIRE(routes.recipientCount() == 2);
    size_t sends = 0;
    routes.forEachDestination([&](uint32_t ip, uint16_t port)
    {
        REQUIRE(ip == client);
        REQUIRE((port == 4000 || port == 49002));
        ++sends;
    });
    // Existing overlapping send rules are preserved, but the display counts each target once.
    REQUIRE(sends == 5);
    routes.gateway = 0x020200C0;
    routes.custom.back() = {0x030200C0, 4500};
    REQUIRE(routes.recipientCount() == 5);
    routes.ports.clear();
    REQUIRE(routes.recipientCount() == 2); // Custom destinations do not need default ports.
    routes.connected = false;
    REQUIRE(routes.recipientCount() == 0);
}

TEST_CASE("UDP recipient storage covers every configured route at maximum capacity", "[udp][monitor]")
{
    Gdl90UdpRoutes routes;
    routes.connected = true;
    routes.network = 0x000200C0;
    for (uint32_t i = 1; i <= GATAS_MAXIMUM_TCP_CLIENTS; ++i)
    {
        routes.clients.insert((i << 24) | routes.network);
    }
    for (uint16_t i = 0; i < GATAS_GDL90OVERUDP_MAX_PORTS; ++i)
    {
        routes.ports.insert(4000 + i);
    }
    routes.gateway = 0x100200C0;
    for (uint32_t i = 0; i < GATAS_GDL90OVERUDP_MAX_CUSTOM_CLIENTS; ++i)
    {
        routes.custom.push_back({((i + 32) << 24) | routes.network, 5000});
    }
    REQUIRE(routes.recipientCount() == (GATAS_MAXIMUM_TCP_CLIENTS + 1) * GATAS_GDL90OVERUDP_MAX_PORTS +
        GATAS_GDL90OVERUDP_MAX_CUSTOM_CLIENTS);
}
