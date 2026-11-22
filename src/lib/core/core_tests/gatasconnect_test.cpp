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

    class PositionReceiver : public etl::message_router<PositionReceiver, GATAS::IngressAircraftPositionsMsg>
    {
    public:
        size_t count = 0;
        GATAS::AircraftPositionInfo last;

        void on_receive(const GATAS::IngressAircraftPositionsMsg &msg)
        {
            count += msg.positions.size();
            last = msg.positions.back();
        }
        void on_receive_unknown(const etl::imessage &) {}
    };
}

TEST_CASE("BLE and UDP both tag COBS aircraft positions as Connect traffic", "[gatasconnect]")
{
    CoreUtils::init();
    etl::message_bus<4> bus;
    MockConfig config{bus};
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    GatasConnect bridge{bus, config, ownship};
    PositionReceiver receiver;
    bus.subscribe(bridge);
    bus.subscribe(receiver);

    uint8_t raw[24] = {1, 0x01, 0x02, 0x03, 1, 4}; // Legacy V1, ICAO, OGN
    auto &pool = BaseModule::getGlobalPool();
    // The initial configuration holds back traffic for the first five receives.
    for (size_t i = 0; i < 5; ++i)
    {
        bus.receive(GATAS::GatasConnectRx{pool, GATAS::GatasConnectTransport::Bluetooth, nullptr, 0});
    }
    for (const auto transport : {GATAS::GatasConnectTransport::Bluetooth, GATAS::GatasConnectTransport::UDP})
    {
        auto *frame = static_cast<uint8_t *>(pool.alloc(getCOBSBufferSize(sizeof(raw))));
        REQUIRE(frame != nullptr);
        const auto length = encodeCOBS(raw, sizeof(raw), frame, getCOBSBufferSize(sizeof(raw)));
        REQUIRE(length > 0);
        const auto expectedCount = receiver.count + 1;
        bus.receive(GATAS::GatasConnectRx{pool, transport, frame, length});
        REQUIRE(expectedCount == receiver.count);
        CHECK(GATAS::TrafficIngress::Connect == receiver.last.ingress);
        CHECK(GATAS::ConspicuityProtocol::OGN == receiver.last.protocol);
    }
}

TEST_CASE("Only qualifying onboard protocols refresh Connect radio traffic", "[gatasconnect]")
{
    CoreUtils::init();
    etl::message_bus<4> bus;
    MockConfig config{bus};
    GATAS::OwnshipState ownship;
    ownship.init(CoreUtils::sharedSpinLock());
    GatasConnect bridge{bus, config, ownship};
    bus.subscribe(bridge);
    time_us_Value = 80'000'000;
    CoreUtils::setPPS(0);

    auto diagnostics = [&]()
    {
        etl::string<512> output;
        etl::string_stream stream(output);
        static_cast<const BaseModule &>(bridge).getData(stream, "");
        return output;
    };

    GATAS::AircraftPositionInfo position;
    position.ingress = GATAS::TrafficIngress::Connect;
    position.protocol = GATAS::ConspicuityProtocol::OGN;
    bus.receive(GATAS::IngressAircraftPositionMsg{position});
    position.ingress = GATAS::TrafficIngress::Transceiver;
    position.protocol = GATAS::ConspicuityProtocol::ADSB;
    bus.receive(GATAS::IngressAircraftPositionMsg{position});
    REQUIRE(diagnostics().find("\"lastRadioTrafficUs\":0") != etl::string<512>::npos);

    position.protocol = GATAS::ConspicuityProtocol::OGN;
    bus.receive(GATAS::IngressAircraftPositionMsg{position});
    REQUIRE(diagnostics().find("\"lastRadioTrafficUs\":80000000") != etl::string<512>::npos);
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
