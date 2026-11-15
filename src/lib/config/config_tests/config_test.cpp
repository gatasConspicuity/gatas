
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

GATAS::ThreadSafeBus<50> bus;

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
        REQUIRE_FALSE(hwConfig.privacy);
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

    SECTION("Aircraft privacy follows the saved setting")
    {
        REQUIRE(config.setData(
            R"=({"callSign":"XX-XXX","address":12345678,"addressType":"OGN","category":"Small","privacy":true})=",
            "/api/Config/aircraft/XX-XXX.json"));
        REQUIRE(config.gaTasConfig().privacy);

        REQUIRE(config.setData(
            R"=({"callSign":"XX-XXX","address":12345678,"addressType":"OGN","category":"Small","privacy":false})=",
            "/api/Config/aircraft/XX-XXX.json"));
        REQUIRE_FALSE(config.gaTasConfig().privacy);
    }

    SECTION("ADSB occupies an RX-only protocol slot")
    {
        REQUIRE(config.setData(
            R"=({"callSign":"XX-XXX","address":12345678,"addressType":"OGN","category":"Small","protocols":["OGN","ADSL","FLARM","FANET",{"ADSB":{"mode":"RX_TX"}}]})=",
            "/api/Config/aircraft/XX-XXX.json"));
        const auto configured = config.gaTasConfig();
        REQUIRE(configured.protocols.size() == 6);
        REQUIRE(configured.protocols.back().dataSource == GATAS::DataSource::ADSB);
        REQUIRE(configured.protocols.back().mode == GATAS::DataSourceMode::RX);
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
        SECTION("Bmp580 I2C pins")
        {
            const auto map = config.pinMap("Bmp580");
            REQUIRE(map.size() == 2);
            REQUIRE(map.at(GATAS::PinType::SDA) == 26);
            REQUIRE(map.at(GATAS::PinType::SCL) == 27);
        }

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
