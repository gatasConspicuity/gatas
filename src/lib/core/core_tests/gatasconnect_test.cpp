#include <catch2/catch_test_macros.hpp>
#include "mockconfig.h"
#include "coreutils.hpp"
#include "ace/cobs.hpp"
#include "../../gatasconnect/ace/gatasconnect.hpp"

namespace
{
    class LegacyBridgeConfig : public MockConfig
    {
    public:
        bool legacyEnabled = false;
        explicit LegacyBridgeConfig(etl::imessage_bus &bus) : MockConfig(bus) {}
        int valueByPath(int fallback, const etl::string_view section, const etl::string_view key) const override
        {
            if (section == "GatasConnect" && key == "enableGdl90Bridge")
            {
                return legacyEnabled;
            }
            return MockConfig::valueByPath(fallback, section, key);
        }
    };

    class OutputReceiver : public etl::message_router<OutputReceiver, GATAS::GatasConnectTx, GATAS::GdlMsg>
    {
    public:
        size_t bluetoothFrames = 0;
        size_t wifiSourceFrames = 0;
        bool bluetoothOnly = false;
        etl::vector<uint8_t, 256> lastFrame;

        void on_receive(const GATAS::GatasConnectTx &msg)
        {
            ++bluetoothFrames;
            bluetoothOnly = msg.output.usesBluetooth() && !msg.output.usesUDP();
            lastFrame.assign(msg.cobsMessage.get(), msg.cobsMessage.get() + msg.length);
        }
        void on_receive(const GATAS::GdlMsg &)
        {
            ++wifiSourceFrames;
        }
        void on_receive_unknown(const etl::imessage &) {}
    };
}

TEST_CASE("GDL90 follows current COBS subscriber state without changing WiFi fanout", "[gatasconnect]")
{
    CoreUtils::init();
    etl::message_bus<4> bus;
    LegacyBridgeConfig config{bus};
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    GatasConnect bridge{bus, config, ownship};
    OutputReceiver output;
    bus.subscribe(bridge);
    bus.subscribe(output);
    GATAS::GdlMsg gdl;
    const uint8_t payload[] = {0x7e, 0, 0x11, 0, 0x7e};
    gdl.msg.assign(payload, payload + sizeof(payload));

    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 0);
    REQUIRE(output.wifiSourceFrames == 1);

    bus.receive(GATAS::GatasConnectBluetoothStateMsg{2});
    bus.receive(GATAS::GatasConnectBluetoothStateMsg{2});
    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 1);
    REQUIRE(output.bluetoothOnly);
    uint8_t decoded[32] = {};
    REQUIRE(decodeCOBS(output.lastFrame.data(), output.lastFrame.size(), decoded, sizeof(decoded)) == sizeof(payload) + 1);
    REQUIRE(decoded[0] == BinaryMessages::DataType::GDL90_V1);
    for (size_t i = 0; i < sizeof(payload); ++i)
    {
        REQUIRE(decoded[i + 1] == payload[i]);
    }

    // One client leaving must not stop another client's stream.
    bus.receive(GATAS::GatasConnectBluetoothStateMsg{1});
    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 2);
    bus.receive(GATAS::GatasConnectBluetoothStateMsg{0});
    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 2);
    REQUIRE(output.wifiSourceFrames == 4);

    // A later subscription resumes automatically; no app enable command is needed.
    bus.receive(GATAS::GatasConnectBluetoothStateMsg{1});
    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 3);
    REQUIRE(output.wifiSourceFrames == 5);
    bus.receive(GATAS::GdlMsg{});
    REQUIRE(output.bluetoothFrames == 3);
}

TEST_CASE("Legacy bridge values and configuration changes cannot disable automatic GDL90", "[gatasconnect]")
{
    CoreUtils::init();
    etl::message_bus<4> bus;
    LegacyBridgeConfig config{bus};
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    GatasConnect bridge{bus, config, ownship};
    OutputReceiver output;
    bus.subscribe(bridge);
    bus.subscribe(output);
    bus.receive(GATAS::GatasConnectBluetoothStateMsg{1});
    GATAS::GdlMsg gdl;
    gdl.msg.push_back(0x7e);
    for (const bool enabled : {false, true, false})
    {
        config.legacyEnabled = enabled;
        bus.receive(GATAS::ConfigUpdatedMsg{config, etl::string_view{GatasConnect::NAME}});
        bus.receive(gdl);
    }
    REQUIRE(output.bluetoothFrames == 3);
    REQUIRE(output.wifiSourceFrames == 3);
}

TEST_CASE("Monitor reports current Bluetooth subscribers", "[gatasconnect][monitor]")
{
    CoreUtils::init();
    etl::message_bus<4> bus;
    LegacyBridgeConfig config{bus};
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    GatasConnect bridge{bus, config, ownship};
    OutputReceiver output;
    bus.subscribe(bridge);
    bus.subscribe(output);
    GATAS::GdlMsg gdl;
    gdl.msg.push_back(0x7e);
    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 0);
    bus.receive(GATAS::GatasConnectBluetoothStateMsg{2});
    bus.receive(gdl);
    REQUIRE(output.bluetoothFrames == 1);
    etl::string<512> json;
    etl::string_stream stream(json);
    static_cast<const BaseModule &>(bridge).getData(stream, {});
    REQUIRE(json.find("\"gdl90BleSubscribers\":2") != json.npos);
}
