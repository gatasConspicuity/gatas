
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ace/messagerouter.hpp"
#include "ace/messages.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include "pico/rand.h"

#define private public

const uint8_t DEFAULT_GATAS_CONFIG[] = R"=(
{"signature": 250801}
)=";

#include "inmemorystore.hpp"
#include "config.hpp"
#include "../../idle/ace/idle.hpp"
#include "../../gatasconnect/ace/gatasconnect.hpp"

class TestIdle : public Idle
{
public:
    using Idle::Idle;
protected:
    void performSystemAction(GATAS::DeviceAction) override {}
};

GATAS::ThreadSafeBus<50> bus;

class FaultStore : public InMemoryStore
{
public:
    enum class Failure { None, Zero, Short, Corrupt };
    Failure failure = Failure::None;
    size_t writes = 0;
    uint8_t *backing;
    FaultStore(size_t capacity, uint8_t *data) : InMemoryStore(capacity, data), backing(data) {}
    size_t write(const uint8_t *data, size_t size) override
    {
        ++writes;
        if (failure == Failure::Zero) { return 0; }
        if (failure == Failure::Short) { return InMemoryStore::write(data, size - 1); }
        const auto written = InMemoryStore::write(data, size);
        if (failure == Failure::Corrupt && written > 0) { backing[0] ^= 1; }
        return written;
    }
};

class SystemActionReceiver : public etl::message_router<SystemActionReceiver, GATAS::SystemActionMsg>
{
public:
    int resets = 0;
    GATAS::DeviceAction lastReset = GATAS::DeviceAction::Status;
    void on_receive(const GATAS::SystemActionMsg &msg)
    {
        ++resets;
        lastReset = msg.action;
    }
    void on_receive_unknown(const etl::imessage &) {}
};

TEST_CASE("Configuration saves and web system actions", "[device-actions]")
{
    uint8_t ram[4096] = {}, flash[4096] = {}, binary[4096] = {};
    InMemoryStore ramStore(4096, ram), binaryStore(4096, binary);
    FaultStore flashStore(4096, flash);
    GATAS::ThreadSafeBus<8> localBus;
    SystemActionReceiver systemActions;
    localBus.subscribe(systemActions);
    Config config(localBus, ramStore, flashStore, binaryStore, DEFAULT_GATAS_CONFIG);
    config.postConstruct();
    REQUIRE_FALSE(config.isDirty());
    config.start();
    TestIdle idle(localBus, config);
    idle.postConstruct();
    localBus.subscribe(idle);
    REQUIRE(config.setData(R"({"aircraftId":"TEST"})", "/api/Config/config.json"));
    REQUIRE(config.isDirty());

    SECTION("SaveBr verifies readback and avoids unchanged flash writes")
    {
        REQUIRE(config.setData("{}", "/api/Config/SaveBr.json"));
        REQUIRE_FALSE(config.isDirty());
        REQUIRE(config.persistentMatchesVolatile());
        REQUIRE(flashStore.writes == 1);
        REQUIRE(config.setData("{}", "/api/Config/SaveBr.json"));
        REQUIRE(flashStore.writes == 1);
    }
    SECTION("Zero short and corrupt writes cannot claim success")
    {
        for (const auto failure : {FaultStore::Failure::Zero, FaultStore::Failure::Short, FaultStore::Failure::Corrupt})
        {
            flashStore.failure = failure;
            REQUIRE_FALSE(config.setData("{}", "/api/Config/SaveBr.json"));
            REQUIRE_FALSE(config.persistentMatchesVolatile());
            REQUIRE(config.isDirty());
            REQUIRE(config.strValueByPath("", "config", "aircraftId") == "TEST");
        }
    }
    SECTION("Web restart requests Idle without writing flash")
    {
        REQUIRE(config.setData("{}", "/api/Config/Restart.json"));
        REQUIRE(systemActions.resets == 1);
        REQUIRE(systemActions.lastReset == GATAS::DeviceAction::Restart);
        REQUIRE(flashStore.writes == 0);
        REQUIRE(config.strValueByPath("", "config", "aircraftId") == "TEST");
    }
    SECTION("Web USB boot requests Idle without writing flash")
    {
        REQUIRE(config.setData("{}", "/api/Config/UsbBoot.json"));
        REQUIRE(systemActions.resets == 1);
        REQUIRE(systemActions.lastReset == GATAS::DeviceAction::UsbBoot);
        REQUIRE(flashStore.writes == 0);
    }
}

class ConfigurationReceiver : public etl::message_router<ConfigurationReceiver, GATAS::GatasConnectTx>
{
public:
    size_t configurationCount = 0;
    uint8_t bootId = 0;
    bool dirty = false;

    void on_receive(const GATAS::GatasConnectTx &msg)
    {
        REQUIRE(msg.output.usesBluetooth());
        REQUIRE(msg.length > 0);
        REQUIRE(msg.cobsMessage.get()[msg.length - 1] == 0);
        for (size_t index = 0; index + 1 < msg.length; ++index)
        {
            REQUIRE(msg.cobsMessage.get()[index] != 0);
        }
        uint8_t raw[256] = {};
        const auto size = decodeCOBS(msg.cobsMessage.get(), msg.length, raw, sizeof(raw));
        REQUIRE(size >= BinaryMessages::serializeAircraftConfigurationSizeV2().items(0));
        REQUIRE(raw[0] == BinaryMessages::DataType::AIRCRAFT_CONFIGURATIONS_V2);
        REQUIRE(raw[13] == GATAS::VERSION_MAJOR);
        REQUIRE(raw[14] == GATAS::VERSION_MINOR);
        REQUIRE(raw[15] == GATAS::VERSION_BUGFIX);
        dirty = (raw[1] & BinaryMessages::AIRCRAFT_CONFIGURATION_DIRTY_FLAG) != 0;
        bootId = raw[16];
        ++configurationCount;
    }
    void on_receive_unknown(const etl::imessage &) {}
};

TEST_CASE("Configuration broadcasts include current device state", "[device-state]")
{
    // Enable the existing semaphore stub only for this test, restoring it on exit.
    struct SemaphoreMockScope
    {
        uint32_t previous = xSemaphoreTakeValue;
        uint64_t previousRandom = get_rand_64();
        SemaphoreMockScope() { xSemaphoreTakeValue = pdTRUE; }
        ~SemaphoreMockScope()
        {
            xSemaphoreTakeValue = previous;
            get_rand_64_SET(previousRandom);
        }
    } semaphoreMock;

    uint8_t ram[4096] = {}, flash[4096] = {}, binary[4096] = {};
    InMemoryStore ramStore(sizeof(ram), ram), flashStore(sizeof(flash), flash), binaryStore(sizeof(binary), binary);
    GATAS::ThreadSafeBus<8> localBus;
    Config config(localBus, ramStore, flashStore, binaryStore, DEFAULT_GATAS_CONFIG);
    config.postConstruct();
    GATAS::OwnshipState ownshipState;
    GatasConnect connect(localBus, config, ownshipState);
    localBus.subscribe(connect);
    ConfigurationReceiver receiver;
    localBus.subscribe(receiver);
    get_rand_64_SET(0x0102030405060708ULL);
    // The timer stub returns null, but boot ID initialization still runs.
    connect.postConstruct();

    connect.sendAircraftConfiguration();
    REQUIRE(receiver.configurationCount == 1);
    REQUIRE(receiver.bootId == 0x08);
    REQUIRE_FALSE(receiver.dirty);

    REQUIRE(config.setData(R"({"value":1})", "/api/Config/Other.json"));
    connect.sendAircraftConfiguration();
    REQUIRE(receiver.dirty);
    REQUIRE(receiver.bootId == 0x08);

    REQUIRE(config.setData("{}", "/api/Config/SaveBr.json"));
    connect.sendAircraftConfiguration();
    REQUIRE(receiver.configurationCount == 3);
    REQUIRE_FALSE(receiver.dirty);
    REQUIRE(receiver.bootId == 0x08);

    SECTION("Zero random boot ID is replaced with a nonzero ID")
    {
        get_rand_64_SET(0);
        connect.postConstruct();
        connect.sendAircraftConfiguration();
        REQUIRE(receiver.bootId != 0);
    }
    SECTION("A zero low byte still produces a nonzero boot ID")
    {
        get_rand_64_SET(0x0102030405060700ULL);
        connect.postConstruct();
        connect.sendAircraftConfiguration();
        REQUIRE(receiver.bootId == 1);
    }
}

TEST_CASE("InMemoryStore capacity", "[single-file]")
{
    uint8_t data[4] = {};
    InMemoryStore store{sizeof(data), data};
    const uint8_t exactFit[] = {1, 2, 3, 4};

    REQUIRE(store.capacity() == sizeof(data));
    REQUIRE(store.writtenSize() == 0);
    REQUIRE(store.write(exactFit, sizeof(exactFit)) == sizeof(exactFit));
    REQUIRE(store.writtenSize() == sizeof(data));
    REQUIRE(store.write(5) == 0);

    store.rewind();
    REQUIRE(store.writtenSize() == 0);
    REQUIRE(store.write(exactFit, sizeof(exactFit) + 1) == 0);
    REQUIRE(store.writtenSize() == 0);
}

TEST_CASE("Too-small flash cannot damage the complete RAM configuration", "[device-actions]")
{
    uint8_t ram[4096] = {}, flash[16] = {}, binary[4096] = {};
    InMemoryStore ramStore(sizeof(ram), ram), binaryStore(sizeof(binary), binary);
    FaultStore flashStore(sizeof(flash), flash);
    GATAS::ThreadSafeBus<8> localBus;
    Config config(localBus, ramStore, flashStore, binaryStore, DEFAULT_GATAS_CONFIG);
    config.postConstruct();
    REQUIRE(config.setData(R"({"aircraftId":"KEEP-THIS"})", "/api/Config/config.json"));
    const auto before = std::vector<uint8_t>(ram, ram + ramStore.writtenSize());
    REQUIRE_FALSE(config.setData("{}", "/api/Config/SaveBr.json"));
    REQUIRE(flashStore.writes == 0);
    REQUIRE(std::memcmp(before.data(), ram, before.size()) == 0);
    REQUIRE(config.strValueByPath("", "config", "aircraftId") == "KEEP-THIS");
}

TEST_CASE("Fully Configured", "[single-file]")
{
    uint8_t vstore[4096] = {};
    uint8_t pstore[4096] = {};
    uint8_t btore[4096] = {};

    InMemoryStore volatileStore{4096, vstore};
    InMemoryStore binaryStore{4096, btore};
    InMemoryStore permanentStore{4096, pstore};
    // std::filesystem::path currentDir = std::filesystem::current_path();
    // std::cout << "Current execution directory: " << currentDir << std::endl;

    std::ifstream inputFile("./test.json"); // Open the file
    REQUIRE(inputFile.is_open());

    inputFile.seekg(0, std::ios::end);
    std::streampos fileSize = inputFile.tellg();
    std::vector<char> buffer(fileSize);

    // Determine the size of the file
    inputFile.seekg(0, std::ios::beg);
    inputFile.read(buffer.data(), fileSize);
    inputFile.close();
    REQUIRE(inputFile.gcount() == fileSize);

    permanentStore.write((uint8_t *)buffer.data(), fileSize);
    Config config(bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG);
    config.postConstruct();



    SECTION("AircraftHwConfig")
    {
        auto hwConfig = config.gaTasConfig();
        REQUIRE(static_cast<uint32_t>(12345678) == static_cast<uint32_t>(hwConfig.conspicuity.icaoAddress));
        REQUIRE(GATAS::AddressType::OGN == hwConfig.conspicuity.addressType);
        REQUIRE(GATAS::AircraftCategory::SMALL == hwConfig.conspicuity.category);
        //        REQUIRE ( (hwConfig.privacy == 0) );
        REQUIRE(4 == hwConfig.protocols.size());
        REQUIRE(GATAS::DataSource::OGN == hwConfig.protocols[0].dataSource);
        REQUIRE(GATAS::DataSourceMode::RX_TX == hwConfig.protocols[0].mode);
        REQUIRE(GATAS::DataSource::ADSLM == hwConfig.protocols[1].dataSource);
        REQUIRE(GATAS::DataSourceMode::RX_TX == hwConfig.protocols[1].mode);
        REQUIRE(GATAS::DataSource::ADSLO_HDR == hwConfig.protocols[2].dataSource);
        REQUIRE(GATAS::DataSourceMode::RX_TX == hwConfig.protocols[2].mode);
        REQUIRE(GATAS::DataSource::FLARM == hwConfig.protocols[3].dataSource);
        REQUIRE(GATAS::DataSourceMode::RX_TX == hwConfig.protocols[3].mode);
    }

    SECTION("Arrays")
    {
        REQUIRE(4000 == config.valueByPath(1, "GDLoverUDP/defaultPorts/0", ""));
        REQUIRE(4001 == config.valueByPath(1, "GDLoverUDP/defaultPorts/1", ""));

        REQUIRE(4000 == config.valueByPath(1, "GDLoverUDP", "defaultPorts/0"));
        REQUIRE(4001 == config.valueByPath(1, "GDLoverUDP", "defaultPorts/1"));

        auto value = config.strValueByPath("-", "GDLoverUDP/ips/0", "ip");
        etl::string<24> expect = "192.168.178.192";
        REQUIRE(expect == value);
    }

    SECTION("pinMap")
    {
        SECTION("valid")
        {
            GATAS::PinTypeMap map = config.pinMap("Sx1262_1");
            REQUIRE(map.size() == 4);
            REQUIRE(map[GATAS::PinType::BUSY] == 13);
            REQUIRE(map[GATAS::PinType::CS] == 12);
            REQUIRE(map[GATAS::PinType::DIO1] == 19);
            REQUIRE(map[GATAS::PinType::SPI] == 0);
        }

        SECTION("fallback")
        {
            GATAS::PinTypeMap map = config.pinMap("AceSpi_1", "AceSpi");
            REQUIRE(map.size() == 5);
            REQUIRE(map[GATAS::PinType::CLK] == 2);
            REQUIRE(map[GATAS::PinType::MOSI] == 3);
            REQUIRE(map[GATAS::PinType::MISO] == 4);
            REQUIRE(map[GATAS::PinType::RST] == 5);
            REQUIRE(map[GATAS::PinType::SPI] == 0);
        }

        SECTION("No fallback")
        {
            GATAS::PinTypeMap map = config.pinMap("AceSpi_0", "AceSpi");
            REQUIRE(map.size() == 5);
            REQUIRE(map[GATAS::PinType::CLK] == 12);
            REQUIRE(map[GATAS::PinType::MOSI] == 13);
            REQUIRE(map[GATAS::PinType::MISO] == 14);
            REQUIRE(map[GATAS::PinType::RST] == 15);
            REQUIRE(map[GATAS::PinType::SPI] == 10);
        }

        SECTION("NoPort andInvalidPort")
        {
            GATAS::PinTypeMap map = config.pinMap("NoPort");
            REQUIRE((map.size() == 0));
            map = config.pinMap("InvalidPort");
            REQUIRE((map.size() == 0));
        }
    }

    SECTION("value by path")
    {
        config.setValueBypath("/Bluetooth/localName", "bas");
        REQUIRE("bas" == config.strValueByPath("default", "Bluetooth", "localName"));
        config.setValueBypath("/Bluetooth/localName", "bar");
        REQUIRE("bar" == config.strValueByPath("default", "Bluetooth", "localName"));

        config.setValueBypath("/ADSBDecoder/filterAbove", 12);
        REQUIRE(12 == config.valueByPath(0, "ADSBDecoder", "filterAbove"));
        config.setValueBypath("/ADSBDecoder/filterAbove", 15);
        REQUIRE(15 == config.valueByPath(0, "ADSBDecoder", "filterAbove"));
        REQUIRE(config.floatValueByPath(0.0f, "ADSBDecoder", "filterAbove") == 15.0f);

        config.setValueBypath("/test/position", "53.123456");
        REQUIRE(config.floatValueByPath(0.0f, "test", "position") == 53.123456f);
        config.setValueBypath("/test/position", "position");
        REQUIRE(config.floatValueByPath(12.5f, "test", "position") == 12.5f);
        REQUIRE(config.floatValueByPath(7.5f, "test", "missing") == 7.5f);
    }

    SECTION("Config status includes gatasId")
    {
        etl::string<256> output;
        etl::string_stream stream(output);

        config.getData(stream, "/api/Config.json");

        JsonDocument status;
        REQUIRE(deserializeJson(status, output.c_str()) == DeserializationError::Ok);
        REQUIRE(status["gatasId"].is<uint32_t>());
    }

    SECTION("Persistent comparison survives store rewind")
    {
        REQUIRE(config.setData("{}", "/api/Config/SaveBr.json") == true);
        REQUIRE(config.persistentMatchesVolatile());

        // A restart resets both stores' runtime write positions, but their
        // backing bytes remain present.
        volatileStore.rewind();
        permanentStore.rewind();
        Config restartedConfig(bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG);
        REQUIRE(restartedConfig.postConstruct() == GATAS::PostConstruct::OK);

        REQUIRE(volatileStore.writtenSize() > 0);
        REQUIRE(permanentStore.writtenSize() == 0);
        REQUIRE(restartedConfig.persistentMatchesVolatile());

        pstore[0] ^= 0x01;
        REQUIRE_FALSE(restartedConfig.persistentMatchesVolatile());
    }

    SECTION("BLE aircraft selection survives a warm restart without saving flash")
    {
        REQUIRE(config.setData(R"({"callSign":"YY-YYY","address":10994642,"addressType":"FLARM","category":"Small"})",
            "/api/Config/aircraft/YY-YYY.json"));
        config.on_receive(GATAS::SelectAircraftRequestMsg{10994642});
        REQUIRE(config.gaTasConfig().conspicuity.icaoAddress == 10994642);
        REQUIRE_FALSE(config.persistentMatchesVolatile());
        Config restarted(bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG);
        REQUIRE(restarted.postConstruct() == GATAS::PostConstruct::OK);
        REQUIRE(restarted.gaTasConfig().conspicuity.icaoAddress == 10994642);
    }

    SECTION("Verified save survives loss of the retained RAM image")
    {
        config.setValueBypath("/Bluetooth/localName", "SAVED-COLD");
        REQUIRE(config.setData("{}", "/api/Config/SaveBr.json"));
        memset(vstore, 0, sizeof(vstore));
        Config coldBoot(bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG);
        REQUIRE(coldBoot.postConstruct() == GATAS::PostConstruct::OK);
        REQUIRE(coldBoot.strValueByPath("", "Bluetooth", "localName") == "SAVED-COLD");
        REQUIRE(coldBoot.persistentMatchesVolatile());
    }

#if GATAS_DEBUG == 1
    SECTION("Erase stored configuration")
    {
        REQUIRE(config.setData("{}", "/api/Config/EraseBr.json") == true);

        for (auto value : pstore)
        {
            REQUIRE(value == 0xFF);
        }

        for (auto value : vstore)
        {
            REQUIRE(value == 0xFF);
        }

        REQUIRE(config.valueByPath(1, "signature", "") == 250801);
    }
#endif

    SECTION("Invalid JSON does not partially overwrite an aircraft")
    {
        REQUIRE(config.valueByPath(0, "aircraft/XX-XXX", "address") == 12345678);
        REQUIRE(config.strValueByPath("", "aircraft/XX-XXX", "category") == "Small");

        const etl::string_view partialAircraft =
            R"=({"callSign":"XX-XXX","address":109,"addressType":"FLARM","category":)=";
        REQUIRE(config.setData(partialAircraft, "/api/Config/aircraft/XX-XXX.json") == false);

        REQUIRE(config.valueByPath(0, "aircraft/XX-XXX", "address") == 12345678);
        REQUIRE(config.strValueByPath("", "aircraft/XX-XXX", "addressType") == "OGN");
        REQUIRE(config.strValueByPath("", "aircraft/XX-XXX", "category") == "Small");
    }

    SECTION("A complete aircraft update is applied atomically")
    {
        const etl::string_view aircraft =
            R"=({"callSign":"XX-XXX","address":10994641,"addressType":"FLARM","category":"Surface Vehicle","privacy":0,"noTrack":0,"protocols":["OGN","FLARM_TX","ADSL","FANET"]})=";
        REQUIRE(config.setData(aircraft, "/api/Config/aircraft/XX-XXX.json") == true);

        REQUIRE(config.valueByPath(0, "aircraft/XX-XXX", "address") == 10994641);
        REQUIRE(config.strValueByPath("", "aircraft/XX-XXX", "addressType") == "FLARM");
        REQUIRE(config.strValueByPath("", "aircraft/XX-XXX", "category") == "Surface Vehicle");
    }

    SECTION("A new aircraft can be created atomically")
    {
        REQUIRE(config.setData(
                    R"=({"callSign":"BROKEN","address":)=",
                    "/api/Config/aircraft/BROKEN.json") == false);
        REQUIRE(config.strValueByPath("missing", "aircraft/BROKEN", "callSign") == "missing");

        const etl::string_view aircraft =
            R"=({"callSign":"TEST-1","address":1193046,"addressType":"OGN","category":"Small","privacy":0,"noTrack":0,"protocols":[{"OGN":{"mode":"RX"}}]})=";
        REQUIRE(config.setData(aircraft, "/api/Config/aircraft/TEST-1.json") == true);

        REQUIRE(config.valueByPath(0, "aircraft/TEST-1", "address") == 1193046);
        REQUIRE(config.strValueByPath("", "aircraft/TEST-1", "callSign") == "TEST-1");
        REQUIRE(config.strValueByPath("", "aircraft/TEST-1", "addressType") == "OGN");
        REQUIRE(config.strValueByPath("", "aircraft/TEST-1", "category") == "Small");
    }
}

TEST_CASE("Config GATAS::PostConstruct", "[single-file]")
{
    uint8_t vstore[4096] = {};
    uint8_t pstore[4096] = {};
    uint8_t btore[4096] = {};

    InMemoryStore volatileStore{4096, vstore};
    InMemoryStore binaryStore{4096, btore};
    InMemoryStore permanentStore{4096, pstore};

    const uint8_t WRONG[] = "INIT WRONG";
    permanentStore.write(WRONG, sizeof(WRONG));

    SECTION("Default if stores are wrong")
    {
        get_rand_64_SET(1234);
        Config testCfg{bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG};
        REQUIRE(testCfg.postConstruct() == GATAS::PostConstruct::OK);
        REQUIRE(250801 == testCfg.valueByPath(1, "signature", ""));
        REQUIRE(testCfg.internalStore()->magic == GATAS::BinaryStore::MAGIC);
        REQUIRE(testCfg.internalStore()->gatasId == 1234);
    }

    SECTION("Init internalstore just once")
    {
        get_rand_64_SET(1234);
        Config testCfg{bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG};
        testCfg.postConstruct();
        REQUIRE(testCfg.internalStore()->gatasId == 1234);

        get_rand_64_SET(12345);
        testCfg.postConstruct();
        REQUIRE(testCfg.internalStore()->gatasId == 1234);
    }
}

TEST_CASE("Compact device actions reuse configuration save and Idle reset routes", "[compact-device-actions]")
{
    struct SemaphoreMockScope
    {
        uint32_t previous = xSemaphoreTakeValue;
        SemaphoreMockScope() { xSemaphoreTakeValue = pdTRUE; }
        ~SemaphoreMockScope() { xSemaphoreTakeValue = previous; }
    } semaphoreMock;
    uint8_t ram[4096] = {}, flash[4096] = {}, binary[4096] = {};
    InMemoryStore ramStore(sizeof(ram), ram), binaryStore(sizeof(binary), binary);
    FaultStore flashStore(sizeof(flash), flash);
    GATAS::ThreadSafeBus<8> localBus;
    SystemActionReceiver systemActions;
    localBus.subscribe(systemActions);
    Config config(localBus, ramStore, flashStore, binaryStore, DEFAULT_GATAS_CONFIG);
    REQUIRE(config.postConstruct() == GATAS::PostConstruct::OK);
    config.start();
    REQUIRE(config.setData(R"({"value":1})", "/api/Config/Other.json"));
    REQUIRE(config.isDirty());

    SECTION("Save clears dirty state and writes the current configuration")
    {
        localBus.receive(GATAS::DeviceActionRequestMsg{GATAS::DeviceAction::Save});
        REQUIRE(config.isDirty());
        REQUIRE(flashStore.writes == 0);
        localBus.receive(GATAS::IdleMsg{});
        REQUIRE_FALSE(config.isDirty());
        REQUIRE(flashStore.writes == 1);
        REQUIRE(config.persistentMatchesVolatile());
        REQUIRE(systemActions.resets == 0);
        localBus.receive(GATAS::IdleMsg{});
        REQUIRE(flashStore.writes == 1);
    }
    SECTION("Web SaveBr shares the same save behavior")
    {
        REQUIRE(config.setData("{}", "/api/Config/SaveBr.json"));
        REQUIRE_FALSE(config.isDirty());
        REQUIRE(flashStore.writes == 1);
        REQUIRE(config.persistentMatchesVolatile());
        REQUIRE(systemActions.resets == 0);
    }
    SECTION("Restart preserves unsaved configuration without writing flash")
    {
        localBus.receive(GATAS::DeviceActionRequestMsg{GATAS::DeviceAction::Restart});
        REQUIRE(systemActions.resets == 0);
        localBus.receive(GATAS::IdleMsg{});
        REQUIRE(systemActions.resets == 1);
        REQUIRE(systemActions.lastReset == GATAS::DeviceAction::Restart);
        localBus.receive(GATAS::IdleMsg{});
        REQUIRE(systemActions.resets == 1);
        REQUIRE(config.isDirty());
        REQUIRE(flashStore.writes == 0);
    }
    SECTION("USB boot forwards the action without implicitly saving")
    {
        localBus.receive(GATAS::DeviceActionRequestMsg{GATAS::DeviceAction::UsbBoot});
        REQUIRE(systemActions.resets == 0);
        localBus.receive(GATAS::IdleMsg{});
        REQUIRE(systemActions.resets == 1);
        REQUIRE(systemActions.lastReset == GATAS::DeviceAction::UsbBoot);
        localBus.receive(GATAS::IdleMsg{});
        REQUIRE(systemActions.resets == 1);
        REQUIRE(flashStore.writes == 0);
    }
}

TEST_CASE("Pending device actions keep only the latest request", "[compact-device-actions]")
{
    struct SemaphoreMockScope
    {
        uint32_t previous = xSemaphoreTakeValue;
        SemaphoreMockScope() { xSemaphoreTakeValue = pdTRUE; }
        ~SemaphoreMockScope() { xSemaphoreTakeValue = previous; }
    } semaphoreMock;
    uint8_t ram[4096] = {}, flash[4096] = {}, binary[4096] = {};
    InMemoryStore ramStore(sizeof(ram), ram), binaryStore(sizeof(binary), binary);
    FaultStore flashStore(sizeof(flash), flash);
    GATAS::ThreadSafeBus<8> localBus;
    SystemActionReceiver receiver;
    localBus.subscribe(receiver);
    Config config(localBus, ramStore, flashStore, binaryStore, DEFAULT_GATAS_CONFIG);
    REQUIRE(config.postConstruct() == GATAS::PostConstruct::OK);
    config.start();
    localBus.receive(GATAS::IdleMsg{});
    REQUIRE(receiver.resets == 0);
    REQUIRE(flashStore.writes == 0);
    localBus.receive(GATAS::DeviceActionRequestMsg{GATAS::DeviceAction::Save});
    localBus.receive(GATAS::DeviceActionRequestMsg{GATAS::DeviceAction::Restart});
    localBus.receive(GATAS::IdleMsg{});
    REQUIRE(receiver.resets == 1);
    REQUIRE(receiver.lastReset == GATAS::DeviceAction::Restart);
    REQUIRE(flashStore.writes == 0);
    localBus.receive(GATAS::IdleMsg{});
    REQUIRE(receiver.resets == 1);
}
