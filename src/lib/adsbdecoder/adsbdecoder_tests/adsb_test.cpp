
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "math.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdio.h>
#include "testhelpers.h"
#include "mockconfig.h"
#include "semphr.h"
#include "ace/coreutils.hpp"
#include "../../dump1090Client/ace/dump1090client.hpp"

#define private public

#include "adsbdecoder.hpp"

class Test : public etl::message_router<Test, GATAS::IngressAircraftPositionMsg, GATAS::RadioRxManchesterMsg>
{

public:
    GATAS::AircraftPositionInfo position;
    etl::imessage_bus &bus;
    bool received = false;
    int16_t rssiDbm = INT16_MIN;
    size_t binaryFrames = 0;
    Test(etl::imessage_bus &bus_) : bus(bus_)
    {
        bus.subscribe(*this);
    }
    ~Test()
    {
        bus.unsubscribe(*this);
    }

    void on_receive(const GATAS::IngressAircraftPositionMsg &msg)
    {
//        printf("AircraftPosition Received\n");
        received = true;
        position = msg.position;
        rssiDbm = msg.rssidBm;
    }
    void on_receive(const GATAS::RadioRxManchesterMsg &)
    {
        ++binaryFrames;
    }
    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }
};

etl::message_bus<4> bus;
MockConfig mockConfig{bus};
GATAS::OwnshipState ownshipState;

static void sendDump1090Frame(etl::imessage_bus &bus, const uint8_t *frame)
{
    constexpr auto length = GATAS::MAX_MODE_S_MESSAGE_LENGTH;
    auto &pool = BaseModule::getGlobalPool();
    auto *copy = static_cast<uint8_t *>(pool.alloc(length));
    REQUIRE(copy != nullptr);
    auto *error = static_cast<uint8_t *>(pool.alloc(length));
    REQUIRE(error != nullptr);
    etl::copy_n(frame, length, copy);
    etl::fill_n(error, length, 0);
    bus.receive(GATAS::RadioRxManchesterMsg{
        pool,
        copy,
        error,
        length,
        0,
        1'090'000'000,
        GATAS::ConspicuityProtocol::ADSB,
        INT8_MIN,
        GATAS::TrafficIngress::Dump1090});
}

auto ownship = []()
{
    GATAS::OwnshipPositionInfo position{};
    position.timestamp = CoreUtils::timeUs32();
    position.lat = 52.2f;
    position.lon = 4.2f;
    position.ellipseHeight = 1500; // Altitude above WGS84 ellipsoid in meters
    position.groundSpeed = 50.0f;
    position.geoidSeparation = 20; // Height of geoid above WGS84 ellipsoid
    position.airborne = true;
    position.conspicuity = {0x123456, GATAS::AircraftCategory::LIGHT, GATAS::AddressType::ICAO, false, false};
    return position;
}();


// This test needs a bit more validation
TEST_CASE("Test filter below and above", "[single-file]")
{
    etl::message_bus<4> bus;
    MockConfig mockConfig{bus};
    xSemaphoreTakeValue = pdTRUE;
    ADSBDecoder adsbDecoder{bus, mockConfig, ownshipState};
    adsbDecoder.on_receive(GATAS::AdapativeRadiusMsg{10'000'000});
    adsbDecoder.postConstruct();
    adsbDecoder.start();
    uint8_t data[24];
    Test test{bus};

    namespace fs = std::filesystem;
    std::string filename = "adsb.txt";
    auto path = fs::current_path();
    printf("->-> Path: %s\n", path.c_str());
    std::ifstream infile(filename);

    REQUIRE (infile.is_open());

    int higestPlane = 0;
    int lowestPlane = 50000;

    std::string line;
    ownship.ellipseHeight = 10000;
    ownshipState.location.store(ownship);
    adsbDecoder.filterAbove = 50000;
    adsbDecoder.filterBelow = 50000;
    while (std::getline(infile, line))
    {
        test.received = false;
        get_absolute_timeValue += 1;
        CoreUtils::hexStrToByteArray(line.c_str() + 1, data);
        sendDump1090Frame(bus, data);

        if (test.received)
        {
            REQUIRE(test.position.address != 0);

            if (test.position.ellipseHeight > higestPlane)
            {
                higestPlane = test.position.ellipseHeight;
            }
            if (test.position.ellipseHeight < lowestPlane)
            {
                lowestPlane = test.position.ellipseHeight;
            }
        }
    }
    printf("Highest Plane: %dm lowestPlane: %dm\n", higestPlane, lowestPlane);

    int totalPlanes = 0;
    // Test Filtering planes above me
    infile.clear();                 // clear fail and eof bits
    infile.seekg(0, std::ios::beg); // back to the start!
    adsbDecoder.filterAbove = 1000;
    adsbDecoder.filterBelow = 1000;
    ownship.ellipseHeight = lowestPlane - adsbDecoder.filterAbove;
    ownshipState.location.store(ownship);

    get_absolute_timeValue += 10000000;
    while (std::getline(infile, line))
    {
        test.received = false;
        get_absolute_timeValue += 1;
        CoreUtils::hexStrToByteArray(line.c_str() + 1, data);
        sendDump1090Frame(bus, data);
        if (test.received)
        {
            totalPlanes += 1;
        }
    }
    printf("Total Planes above: %d\n", totalPlanes);
    REQUIRE(totalPlanes == 4);

    // Test Filtering planes below me
    infile.clear();                 // clear fail and eof bits
    infile.seekg(0, std::ios::beg); // back to the start!
    adsbDecoder.postConstruct();    // Also clears current internal status
    adsbDecoder.filterAbove = 1000;
    adsbDecoder.filterBelow = 1000;
    ownship.ellipseHeight = higestPlane;
    ownshipState.location.store(ownship);

    get_absolute_timeValue += 100'000'000;
    totalPlanes = 0;
    while (std::getline(infile, line))
    {
        test.received = false;
        get_absolute_timeValue += 10000;
        time_us_Value += 10000;
        CoreUtils::hexStrToByteArray(line.c_str() + 1, data);
        sendDump1090Frame(bus, data);
        if (test.received)
        {
            REQUIRE(test.position.ellipseHeight >= ownship.ellipseHeight - adsbDecoder.filterBelow);
            REQUIRE(test.position.ellipseHeight <= ownship.ellipseHeight + adsbDecoder.filterAbove);
            totalPlanes += 1;
        }
        adsbDecoder.on_receive(GATAS::Every5SecMsg());
    }
    printf("Total Planes below: %d\n", totalPlanes);
    REQUIRE(4577 == totalPlanes);
    infile.close();
}

TEST_CASE("Test heading and direction received aircraft", "[single-file]")
{
    etl::message_bus<4> bus;
    MockConfig mockConfig{bus};
    xSemaphoreTakeValue = pdTRUE;
    ADSBDecoder adsbDecoder(bus, mockConfig, ownshipState);
    adsbDecoder.on_receive(GATAS::AdapativeRadiusMsg{1'000'000});
    adsbDecoder.postConstruct();
    adsbDecoder.start();
    Test test(bus);
    adsbDecoder.filterAbove = 50000;
    adsbDecoder.filterBelow = 50000;

    test.received = false;
    ownship.lat = 52.1;
    ownship.lon = 4.8;
    ownship.ellipseHeight = 10000;
    ownshipState.location.store(ownship);

    uint8_t data[14];
    CoreUtils::hexStrToByteArray("8d502cd1589992ecbaf1a4140b65", data);
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d502cd15899965802eb001f31d8", data);
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d502cd19908c532903c9cced691", data);
    sendDump1090Frame(bus, data);

    REQUIRE(test.received == true);
    REQUIRE(test.position.address == 0x502CD1);
    REQUIRE(test.position.addressType == GATAS::AddressType::ICAO);
    REQUIRE(test.position.protocol == GATAS::ConspicuityProtocol::ADSB);
    REQUIRE(test.position.aircraftType == GATAS::AircraftCategory::UNKNOWN);
    // Velocity arrives last and reports GNSS height 675 ft below pressure altitude.
    REQUIRE(9029 - 205 == test.position.ellipseHeight);
    REQUIRE(test.position.groundSpeed == Catch::Approx(230.47f).margin(0.01f)); // in m/s
    REQUIRE(test.position.track == 25);                                     // ADSB data shows 25.94.. Should we use floats instead of int?
    REQUIRE(test.position.lat == Catch::Approx(52.3888).margin(0.005));
    REQUIRE(test.position.lon == Catch::Approx(4.7209).margin(0.005));
    REQUIRE(test.position.verticalSpeed == Catch::Approx(4.552).margin(0.001));

    REQUIRE(test.position.distanceFromOwn == Catch::Approx(32551).margin(1));
    REQUIRE(test.position.relNorthFromOwn(ownship) == Catch::Approx(32099).margin(1));
    REQUIRE(test.position.relEastFromOwn(ownship) == Catch::Approx(-5402).margin(1));
    // REQUIRE(test.position.bearingFromOwn == Catch::Approx(351).margin(0.5));
    ownship.lat = test.position.lat;
    ownship.lon = test.position.lon;
    ownship.ellipseHeight = test.position.ellipseHeight;
    time_us_Value += 10'000'000;
    ownship.timestamp = CoreUtils::timeUs32() - 1;
    ownshipState.location.store(ownship);
    CoreUtils::hexStrToByteArray("8d502cd1589992ecbaf1a4140b65", data);
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d502cd15899965802eb001f31d8", data);
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d502cd19908c532903c9cced691", data);
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d502cd15899965802eb001f31d8", data);
    sendDump1090Frame(bus, data);
    const auto selected = ownshipState.selectedPressureAltitude();
    REQUIRE(selected.has_value());
    REQUIRE(GATAS::PressureSource::ADSBDerived == selected->method);
    REQUIRE(9029 == selected->metres);

}

TEST_CASE("Test descending aircraft", "[single-file]")
{
    etl::message_bus<4> bus;
    MockConfig mockConfig{bus};
    xSemaphoreTakeValue = pdTRUE;
    ADSBDecoder adsbDecoder{bus, mockConfig, ownshipState};
    adsbDecoder.on_receive(GATAS::AdapativeRadiusMsg{1'000'000});
    adsbDecoder.postConstruct();
    adsbDecoder.start();
    Test test{bus};
    adsbDecoder.filterAbove = 50000;
    adsbDecoder.filterBelow = 50000;

    test.received = false;
    ownship.lat = 52.1;
    ownship.lon = 4.8;
    ownship.ellipseHeight = 10000;
    ownshipState.location.store(ownship);


    uint8_t data[14];
    CoreUtils::hexStrToByteArray("8d407a055817867d1ce5ecbe8fdd", data); // odd
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d407a0558178312d6eefaca54eb", data); // event
    sendDump1090Frame(bus, data);
    CoreUtils::hexStrToByteArray("8d407a059908e102980c93715499", data);
    sendDump1090Frame(bus, data);

    REQUIRE(test.position.verticalSpeed == Catch::Approx(-0.65024f).margin(0.001f));
}

TEST_CASE("TC19 velocity reports update available measurements independently")
{
    etl::message_bus<4> bus;
    MockConfig config{bus};
    GATAS::OwnshipState state;
    state.location.store(ownship);
    ADSBDecoder decoder{bus, config, state};
    xSemaphoreTakeValue = pdTRUE;
    REQUIRE(GATAS::PostConstruct::OK == decoder.postConstruct());
    decoder.start();
    decoder.on_receive(GATAS::AdapativeRadiusMsg{10'000'000});
    decoder.filterAbove = 50'000;
    decoder.filterBelow = 50'000;
    Test receiver{bus};
    const auto send = [&](const char *hex)
    {
        uint8_t data[14];
        CoreUtils::hexStrToByteArray(hex, data);
        receiver.received = false;
        sendDump1090Frame(bus, data);
    };

    send("8d4850205817867d1ce5ec0fc283");
    send("8d48502058178312d6eefa7b19b5");
    send("8D485020994409940838175B284F"); // Sun, chapter 7, message A
    REQUIRE(receiver.received);
    CHECK(Catch::Approx(159.f * KN_TO_MS) == receiver.position.groundSpeed);
    CHECK(182 == receiver.position.track);
    CHECK(Catch::Approx(-4.22656f).margin(0.001f) == receiver.position.verticalSpeed);

    // Subtype 2: (4-1)*4 east and (5-1)*4 north = 20 kt. Rate unavailable.
    send("8d4850209a000400a00000caee41");
    send("8d48502058178312d6eefa7b19b5");
    REQUIRE(receiver.received);
    CHECK(Catch::Approx(20.f * KN_TO_MS) == receiver.position.groundSpeed);
    CHECK(36 == receiver.position.track);
    CHECK(Catch::Approx(-4.22656f).margin(0.001f) == receiver.position.verticalSpeed);

    // Both airspeed subtypes report heading and a descent of 2304 ft/min.
    for (const char *airspeed : {"8d4850209b06b6af189400ca40dc", "8d4850209c06b6af189400df3802"})
    {
        send(airspeed);
        send("8d48502058178312d6eefa7b19b5");
        REQUIRE(receiver.received);
        CHECK(36 == receiver.position.track);
        CHECK(Catch::Approx(20.f * KN_TO_MS) == receiver.position.groundSpeed);
        CHECK(Catch::Approx(-11.70432f).margin(0.001f) == receiver.position.verticalSpeed);
    }

    // Either missing component leaves the last ground vector intact. Rate code 1 is level.
    const char *partial = GENERATE("8d485020990000002004008f8cc4", "8d48502099000100000400c9fa23");
    send(partial);
    send("8d48502058178312d6eefa7b19b5");
    REQUIRE(receiver.received);
    CHECK(Catch::Approx(20.f * KN_TO_MS) == receiver.position.groundSpeed);
    CHECK(36 == receiver.position.track);
    CHECK(0.f == receiver.position.verticalSpeed);

    send("8d485020990001002004008c860a"); // Both components present and zero knots
    send("8d48502058178312d6eefa7b19b5");
    REQUIRE(receiver.received);
    CHECK(0.f == receiver.position.groundSpeed);
}

TEST_CASE("failed ADSB decoder can be deleted without leaving a registered module", "[lifecycle]")
{
    BaseModule *module = new ADSBDecoder(bus, mockConfig, ownshipState);
    const auto previousMutex = xSemaphoreCreateMutexValue;
    xSemaphoreCreateMutexValue = nullptr;
    const auto result = module->postConstruct();
    xSemaphoreCreateMutexValue = previousMutex;

    REQUIRE(result == GATAS::PostConstruct::MUTEX_ERROR);
    REQUIRE(module->canDelete());
    BaseModule::setModuleStatus(ADSBDecoder::NAME, result);
    delete module;

    const auto &status = BaseModule::registeredModules().at(ADSBDecoder::NAME);
    REQUIRE(status.module == nullptr);
    REQUIRE(status.result == GATAS::PostConstruct::MUTEX_ERROR);
    // Failure must not leave a subscriber referring to the deleted decoder.
    bus.receive(GATAS::AdapativeRadiusMsg{10'000'000});
}

TEST_CASE("ADSB decoder accepts complete DF17 frames from the radio bus")
{
    etl::message_bus<4> radioBus;
    MockConfig config{radioBus};
    ADSBDecoder decoder{radioBus, config, ownshipState};
    xSemaphoreTakeValue = pdTRUE;
    REQUIRE(decoder.postConstruct() == GATAS::PostConstruct::OK);
    decoder.start();

    const uint8_t sample[]{0x8d, 0x40, 0x62, 0x1d, 0x58, 0xc3, 0x82, 0xd6, 0x90, 0xc8, 0xac, 0x28, 0x63, 0xa7};
    auto &pool = BaseModule::getGlobalPool();
    auto send = [&](GATAS::ConspicuityProtocol source, size_t length)
    {
        auto *frame = static_cast<uint8_t *>(pool.alloc(length));
        REQUIRE(frame != nullptr);
        etl::copy_n(sample, length, frame);
        radioBus.receive(GATAS::RadioRxMsg{pool, frame, length, 0, 1'090'000'000, source, -65});
    };

    send(GATAS::ConspicuityProtocol::FANET, sizeof(sample));
    send(GATAS::ConspicuityProtocol::ADSB, sizeof(sample) - 1);
    auto *shortFrame = static_cast<uint8_t *>(pool.alloc(sizeof(sample) - 1));
    REQUIRE(shortFrame != nullptr);
    etl::copy_n(sample, sizeof(sample) - 1, shortFrame);
    auto *shortError = static_cast<uint8_t *>(pool.alloc(sizeof(sample) - 1));
    REQUIRE(shortError != nullptr);
    etl::fill_n(shortError, sizeof(sample) - 1, 0);
    radioBus.receive(GATAS::RadioRxManchesterMsg{
        pool,
        shortFrame,
        shortError,
        sizeof(sample) - 1,
        0,
        1'090'000'000,
        GATAS::ConspicuityProtocol::ADSB,
        INT8_MIN,
        GATAS::TrafficIngress::Dump1090});
    REQUIRE(decoder.adsbDataCollector.size() == 0);
    send(GATAS::ConspicuityProtocol::ADSB, sizeof(sample));
    REQUIRE(decoder.adsbDataCollector.size() == 1);
}

TEST_CASE("Dump1090 Monitor counts frames dropped when the pool is full")
{
    etl::message_bus<4> bus;
    MockConfig config{bus};
    Dump1090Client client{bus, config};
    auto &pool = BaseModule::getGlobalPool();
    std::vector<void *> blocks;
    while (auto *block = pool.alloc(GATAS::MAX_MODE_S_MESSAGE_LENGTH))
    {
        blocks.push_back(block);
    }
    uint8_t valid[] = "*8D407A055817867D1CE5ECBE8FDD;";
    uint8_t malformed[] = "*8D407A055817867D1CE5ECBE8FDG;";
    client.processNewSentence(valid);
    client.processNewSentence(malformed);
    for (auto *block : blocks)
    {
        pool.release(block);
    }
    client.processNewSentence(valid);

    etl::string<128> output;
    etl::string_stream stream(output);
    client.getData(stream, "");
    CHECK(output.find("\"poolDrops:err\":1") != output.npos);
    CHECK(output.find("\"totalReceived:k\":1") != output.npos);
}

TEST_CASE("ADS-B ingress and RSSI come from the position pair")
{
    using GATAS::TrafficIngress;
    const auto expected = GENERATE(TrafficIngress::Transceiver, TrafficIngress::Dump1090);
    etl::message_bus<4> radioBus;
    MockConfig config{radioBus};
    Dump1090Client dump1090{radioBus, config};
    REQUIRE(GATAS::PostConstruct::OK == dump1090.postConstruct());
    ADSBDecoder decoder{radioBus, config, ownshipState};
    xSemaphoreTakeValue = pdTRUE;
    REQUIRE(GATAS::PostConstruct::OK == decoder.postConstruct());
    decoder.start();
    decoder.on_receive(GATAS::AdapativeRadiusMsg{1'000'000});
    decoder.filterAbove = 50'000;
    decoder.filterBelow = 50'000;
    auto location = ownship;
    location.lat = 52.1f;
    location.lon = 4.8f;
    location.ellipseHeight = 10'000;
    ownshipState.location.store(location);
    Test receiver{radioBus};

    uint8_t badMarker[] = "*8D407A055817867D1CE5ECBE8FDD!";
    uint8_t badHex[] = "*8D407A055817867D1CE5ECBE8FDG;";
    dump1090.processNewSentence(badMarker);
    dump1090.processNewSentence(badHex);
    REQUIRE(0 == receiver.binaryFrames);

    auto send = [&](bool onboard, const char *hex, int8_t rssiDbm = -80)
    {
        constexpr size_t length = 14;
        if (onboard)
        {
            auto &pool = BaseModule::getGlobalPool();
            auto *frame = static_cast<uint8_t *>(pool.alloc(length));
            REQUIRE(frame != nullptr);
            CoreUtils::hexStrToByteArray(hex, frame);
            radioBus.receive(GATAS::RadioRxMsg{pool, frame, length, 0, 1'090'000'000, GATAS::ConspicuityProtocol::ADSB, rssiDbm});
        }
        else
        {
            etl::string<length * 2 + 2> line("*");
            line.append(hex);
            line.push_back(';');
            dump1090.processNewSentence({reinterpret_cast<uint8_t *>(line.data()), line.size() + 1});
        }
    };

    send(expected == TrafficIngress::Transceiver, "8d407a05202cc371c32ce08133fc"); // A0, category unavailable
    send(expected == TrafficIngress::Transceiver, "8d407a05272cc371c32ce0944b22"); // A7, same callsign
    send(expected == TrafficIngress::Transceiver, "8d407a055817867d1ce5ecbe8fdd"); // Odd
    send(expected != TrafficIngress::Transceiver, "8d407a0558178312d6eefaca54eb"); // Rejected even frame
    SECTION("A velocity report cannot complete a mixed-ingress position pair")
    {
        send(expected == TrafficIngress::Transceiver, "8d407a059908e102980c93715499", -40);
        REQUIRE_FALSE(receiver.received);
        send(expected == TrafficIngress::Transceiver, "8d407a0558178312d6eefaca54eb", -65);
    }
    SECTION("A later velocity report publishes the position pair's RSSI")
    {
        send(expected == TrafficIngress::Transceiver, "8d407a0558178312d6eefaca54eb", -65);
        REQUIRE_FALSE(receiver.received);
        send(expected == TrafficIngress::Transceiver, "8d407a059908e102980c93715499", -40);
    }
    REQUIRE(receiver.received);
    REQUIRE(GATAS::AircraftCategory::ROTORCRAFT == receiver.position.aircraftType);
    REQUIRE(1097 == receiver.position.pressureAlt); // 3,600 ft
    REQUIRE(expected == receiver.position.ingress);
    REQUIRE(GATAS::ConspicuityProtocol::ADSB == receiver.position.protocol);
    REQUIRE((expected == TrafficIngress::Dump1090 ? INT16_MIN : -65) == receiver.rssiDbm);
}

TEST_CASE("ADS-B pressure altitude expires five seconds after the last valid barometric frame")
{
    etl::message_bus<4> bus;
    MockConfig config{bus};
    GATAS::OwnshipState state;
    state.location.store(ownship);
    ADSBDecoder decoder{bus, config, state};
    xSemaphoreTakeValue = pdTRUE;
    REQUIRE(GATAS::PostConstruct::OK == decoder.postConstruct());
    decoder.start();
    decoder.on_receive(GATAS::AdapativeRadiusMsg{10'000'000});
    decoder.filterAbove = 50'000;
    decoder.filterBelow = 50'000;
    Test receiver{bus};
    auto send = [&](const char *hex)
    {
        receiver.received = false;
        uint8_t frame[14];
        CoreUtils::hexStrToByteArray(hex, frame);
        sendDump1090Frame(bus, frame);
    };

    time_us_Value = 10'000'000;
    send("8d407a059908e102980c93715499"); // Velocity
    send("8d407a055817867d1ce5ecbe8fdd"); // Odd, 3,600 ft
    send("8d407a0558058312d6eefa722dae"); // Even, valid zero feet
    REQUIRE(receiver.received);
    CHECK(0 == receiver.position.pressureAlt);

    SECTION("Reported height difference replaces the geoid estimate, including zero")
    {
        const auto geoid = CoreUtils::egmGeoidOffset(receiver.position.lat, receiver.position.lon);
        REQUIRE(0 != geoid);
        const auto checkHeight = [&](const char *velocity, int32_t expected)
        {
            send(velocity);
            send("8d407a0558058312d6eefa722dae"); // Pressure altitude 0 ft
            REQUIRE(receiver.received);
            CHECK(0 == receiver.position.pressureAlt);
            CHECK(expected == receiver.position.ellipseHeight);
        };
        checkHeight("8d407a059908e102980c0376b281", 15);  // +50 ft
        checkHeight("8d407a059908e102980c8371b441", -15); // -50 ft
        checkHeight("8d407a059908e102980c7e8bacda", 952);  // +3125 ft
        checkHeight("8d407a059908e102980c7f7458d3", geoid); // Positive saturation
        CHECK_FALSE(decoder.adsbDataCollector.current().gnssMinusBaroMetres.has_value());
        checkHeight("8d407a059908e102980cfe8caa1a", -952); // -3125 ft
        checkHeight("8d407a059908e102980cff735e13", geoid); // Negative saturation
        CHECK_FALSE(decoder.adsbDataCollector.current().gnssMinusBaroMetres.has_value());
        checkHeight("8d407a059908e102980c0176ae9a", 0);   // Reported zero
        checkHeight("8d407a059908e102980c00895a93", geoid);
        checkHeight("8d407a059908e102980c808e5c53", geoid); // Sign bit only
    }

    time_us_Value += 5'000'000;
    send("8d407a05a0178312d6eefabeb386"); // GNSS height, 3,600 ft
    REQUIRE(receiver.received);
    CHECK(1097 == receiver.position.ellipseHeight);
    CHECK(0 == receiver.position.pressureAlt);

    ++time_us_Value;
    send("8d407a0558000312d6eefad6e8ef"); // Unavailable barometric altitude
    REQUIRE(receiver.received);
    CHECK(GATAS::INVALID_BARO_ALTITUDE == receiver.position.pressureAlt);

    send("8d407a0558080312d6eefa80085b"); // Gillham, -1,200 ft
    REQUIRE(receiver.received);
    CHECK(-365 == receiver.position.pressureAlt);

    send("8d407a0558084312d6eefa42bbdb"); // Gillham, 126,700 ft
    REQUIRE(receiver.received);
    CHECK(38618 == receiver.position.pressureAlt);

    send("8d407a050017800000000067c0e7"); // Type 0, pressure altitude only
    CHECK_FALSE(receiver.received);
    send("8d407a05a0178312d6eefabeb386");
    REQUIRE(receiver.received);
    CHECK(1097 == receiver.position.pressureAlt);
    time_us_Value += uint64_t{1} << 32;
    decoder.on_receive(GATAS::Every5SecMsg{});
    send("8d407a059908e102980c93715499"); // Velocity
    CHECK(1097 == decoder.adsbDataCollector.current().pressureAlt);
    send("8d407a05a017867d1ce5ecca68b0"); // Odd GNSS position
    REQUIRE(receiver.received);
    CHECK(GATAS::INVALID_BARO_ALTITUDE == decoder.adsbDataCollector.current().pressureAlt);
    CHECK(GATAS::INVALID_BARO_ALTITUDE == receiver.position.pressureAlt);

}
