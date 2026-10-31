#include <catch2/catch_test_macros.hpp>

#include "ace/tcpclient.hpp"
#include "../../airconnect/ace/airconnect.hpp"
#include "ace/messagerouter.hpp"
#include "mockconfig.h"

TEST_CASE("TCP startup, reconnect and cleanup keep raw API sequences locked", "[tcp][locking]")
{
    MockTcp::reset();
    TcpClient client{{0x01020304, 2000}, {}, {}};
    REQUIRE(client.start());
    auto &pcb = MockTcp::pcbs[0];
    {
        LwipRAIILock lock;
        REQUIRE(pcb.connected(pcb.arg, &pcb, ERR_OK) == ERR_OK);
    }
    REQUIRE(client.isConnected());
    const uint8_t data[] = {1, 2, 3};
    REQUIRE(client.write(data, true));
    REQUIRE(MockTcp::writes == 1);
    client.stop();
    REQUIRE(client.isDisconnected());
    REQUIRE_FALSE(client.flush());
    client.autoConnect(true);
    REQUIRE_FALSE(client.write(data));
    REQUIRE(MockTcp::allocated == 2);
    client.stop();
    REQUIRE(mockLwipLockDepth == 0);
}

TEST_CASE("Disconnect while entering a write or close never uses the old PCB", "[tcp][locking]")
{
    MockTcp::reset();
    TcpClient client{&MockTcp::pcbs[0], {}};
    mockBeforeLwipLock = [] { MockTcp::disconnect(MockTcp::pcbs[0]); };
    const uint8_t data[] = {1};
    SECTION("write")
    {
        REQUIRE_FALSE(client.write(data));
        REQUIRE(MockTcp::writes == 0);
    }
    SECTION("close")
    {
        client.stop();
    }
    SECTION("flush")
    {
        REQUIRE_FALSE(client.flush());
        REQUIRE(MockTcp::outputs == 0);
    }
    REQUIRE(client.isDisconnected());
    REQUIRE(mockLwipLockDepth == 0);
}

TEST_CASE("TCP startup failures and repeated listener starts release resources", "[tcp][locking]")
{
    MockTcp::reset();
    SECTION("client allocation failure")
    {
        MockTcp::failAllocation = true;
        TcpClient client{{1, 2000}, {}, {}};
        REQUIRE_FALSE(client.start());
        REQUIRE(client.isDisconnected());
    }
    SECTION("connect failure")
    {
        MockTcp::connectResult = ERR_MEM;
        TcpClient client{{1, 2000}, {}, {}};
        REQUIRE_FALSE(client.start());
        REQUIRE_FALSE(MockTcp::pcbs[0].alive);
        REQUIRE(client.isDisconnected());
    }
    SECTION("listener bind failure")
    {
        MockTcp::bindResult = ERR_MEM;
        TcpListener listener{2000, {}};
        REQUIRE_FALSE(listener.start());
        REQUIRE_FALSE(MockTcp::pcbs[0].alive);
    }
    SECTION("listener start is idempotent")
    {
        TcpListener listener{2000, {}};
        REQUIRE(listener.start());
        REQUIRE(listener.start());
        REQUIRE(MockTcp::allocated == 1);
        listener.stop();
        listener.stop();
        REQUIRE_FALSE(MockTcp::pcbs[0].alive);
    }
    REQUIRE(mockLwipLockDepth == 0);
}

TEST_CASE("AirConnect accepts never wait for a producer and reject full lists", "[airconnect][locking]")
{
    MockTcp::reset();
    GATAS::ThreadSafeBus<8> bus;
    MockConfig config{bus};
    AirConnect airConnect{bus, config};
    BaseModule &module = airConnect;
    REQUIRE(module.postConstruct() == GATAS::PostConstruct::OK);
    module.start();
    bus.receive(GATAS::WifiConnectionStateMsg{GATAS::WifiMode::AP});
    auto &listener = MockTcp::pcbs[0];
    tcp_pcb incoming;

    xSemaphoreTakeValue = pdFALSE;
    xSemaphoreTakeWait = 1000;
    {
        LwipRAIILock lock;
        REQUIRE(listener.accept(listener.arg, &incoming, ERR_OK) == ERR_MEM);
    }
    REQUIRE(xSemaphoreTakeWait == 0);
    REQUIRE(incoming.arg == nullptr);

    xSemaphoreTakeValue = pdTRUE;
    for (size_t i = 1; i <= GATAS_MAXIMUM_TCP_CLIENTS; ++i)
    {
        LwipRAIILock lock;
        REQUIRE(listener.accept(listener.arg, &MockTcp::pcbs[i], ERR_OK) == ERR_OK);
    }
    {
        LwipRAIILock lock;
        REQUIRE(listener.accept(listener.arg, &incoming, ERR_OK) == ERR_MEM);
    }
    REQUIRE(incoming.arg == nullptr);

    // Failed mutex acquisition must not append NMEA or traverse the list.
    xSemaphoreTakeValue = pdFALSE;
    const GATAS::NMEAString sentence{"$GPGGA,123456789012345678901234567890123456789012345678901234567890"};
    for (unsigned i = 0; i < 8; ++i)
    {
        bus.receive(GATAS::DataPortMsg{sentence});
    }
    xSemaphoreTakeValue = pdTRUE;
    bus.receive(GATAS::DataPortMsg{sentence});
    REQUIRE(MockTcp::writes == 0);

    for (size_t i = 1; i <= GATAS_MAXIMUM_TCP_CLIENTS; ++i)
    {
        MockTcp::disconnect(MockTcp::pcbs[i]);
    }
    bus.receive(GATAS::Every5SecMsg{});
    {
        LwipRAIILock lock;
        REQUIRE(listener.accept(listener.arg, &incoming, ERR_OK) == ERR_OK);
    }
    MockTcp::disconnect(incoming);
    bus.receive(GATAS::Every5SecMsg{});
    xSemaphoreTakeValue = pdFALSE;
}
