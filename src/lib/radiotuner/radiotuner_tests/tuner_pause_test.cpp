#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "ace/messages.hpp"
#include "mockconfig.h"
#define private public
#include "radiotunerrx_v2.hpp"
#include "radiotunertx_v2.hpp"
#undef private

namespace
{
    struct StopWorker {};
    uint32_t notifications[2];
    size_t notificationCount;
    size_t notificationIndex;
    TickType_t resumedDelay;
    BaseType_t deliverNotifications(uint32_t *value)
    {
        if (notificationIndex == notificationCount)
        {
            resumedDelay = mockTaskNotifyWaitTicks;
            throw StopWorker{};
        }
        *value = notifications[notificationIndex++];
        return pdPASS;
    }
    struct ResetMocks
    {
        ~ResetMocks()
        {
            mockEventWaitMask = UINT32_MAX;
            mockTaskNotifyWaitHook = nullptr;
        }
    };
}

TEST_CASE("Timed out tuner pauses release delayed workers", "[pause]")
{
    const bool rx = GENERATE(true, false);
    const bool coalesced = GENERATE(true, false);
    CAPTURE(rx, coalesced);
    ResetMocks reset;
    CoreUtils::init();
    etl::message_bus<4> bus;
    MockConfig config(bus);
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    RadioTunerRx receiver(bus, config, ownship);
    RadioTunerTx transmitter(bus, config, ownship);
    mockEventWaitMask = 0; // Inject a pause acknowledgement timeout.
    xTaskNotifyCalled = false;
    if (rx)
    {
        REQUIRE_FALSE(receiver.blockTasks());
        REQUIRE(xTaskNotifyCalled);
        REQUIRE(xTaskNotifyulValue == RadioTunerRx::UNBLOCK);
    }
    else
    {
        GATAS::DataSourceConfig protocol{GATAS::DataSource::OGN, GATAS::DataSourceMode::RX_TX};
        transmitter.assignDataSources(etl::span<GATAS::DataSourceConfig>(&protocol, 1));
        REQUIRE(xTaskNotifyCalled);
        REQUIRE(xTaskNotifyulValue == RadioTunerRx::UNBLOCK);
        REQUIRE(transmitter.dataSourceTxEvents.empty());
        REQUIRE(transmitter.configuredDatasources.empty());
    }
    const uint32_t block = RadioTunerRx::BLOCK;
    const uint32_t unblock = xTaskNotifyulValue;
    notifications[0] = coalesced ? block | unblock : block;
    notifications[1] = unblock;
    notificationCount = coalesced ? 1 : 2;
    notificationIndex = 0;
    mockTaskNotifyWaitHook = deliverNotifications;
    if (rx)
    {
        REQUIRE_THROWS_AS(RadioTunerRx::radioTuneTask(&receiver), StopWorker);
        REQUIRE(resumedDelay == TASK_DELAY_MS(1000));
    }
    else
    {
        REQUIRE_THROWS_AS(transmitter.radioTuneTask(), StopWorker);
        REQUIRE(resumedDelay == TASK_DELAY_MS(2000));
    }
}

TEST_CASE("RX configuration changes require a successful pause", "[pause][config]")
{
    const bool pauseSucceeds = GENERATE(false, true);
    ResetMocks reset;
    CoreUtils::init();
    etl::message_bus<4> bus;
    MockConfig original(bus);
    class ChangedConfig : public MockConfig
    {
    public:
        using MockConfig::MockConfig;
        const GATAS::Config::GaTasConfiguration gaTasConfig() const override
        {
            auto config = MockConfig::gaTasConfig();
            config.protocols.clear();
            config.protocols.push_back({GATAS::DataSource::FANET, GATAS::DataSourceMode::RX_TX});
            return config;
        }
    } changed(bus);
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    RadioTunerRx receiver(bus, original, ownship);
    const auto ogn = static_cast<uint8_t>(GATAS::DataSource::OGN);
    receiver.slotReceive[ogn] = 7;
    mockEventWaitMask = pauseSucceeds ? UINT32_MAX : 0;
    receiver.on_receive(GATAS::ConfigUpdatedMsg{changed, Configuration::NAME});
    REQUIRE(receiver.configuredDatasources.size() == 1);
    REQUIRE(receiver.configuredDatasources.front().dataSource ==
            (pauseSucceeds ? GATAS::DataSource::FANET : GATAS::DataSource::OGN));
    REQUIRE(receiver.slotReceive[ogn] == (pauseSucceeds ? 0 : 7));
    REQUIRE(xTaskNotifyulValue == RadioTunerRx::UNBLOCK);
}
