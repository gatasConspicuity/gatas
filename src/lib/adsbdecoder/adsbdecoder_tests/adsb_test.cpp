
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "math.h"
#include <fstream>
#include <filesystem>
#include <stdio.h>
#include "testhelpers.h"
#include "mockconfig.h"
#include "semphr.h"
#include "ace/coreutils.hpp"

#define private public

#include "adsbdecoder.hpp"

class Test : public etl::message_router<Test, GATAS::IngressAircraftPositionMsg>
{

public:
    GATAS::AircraftPositionInfo position;
    etl::imessage_bus &bus;
    bool received = false;
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
    }
    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }
};

etl::message_bus<4> bus;
MockConfig mockConfig{bus};
GATAS::OwnshipState ownshipState;

auto ownship = GATAS::OwnshipPositionInfo{
    CoreUtils::timeUs32(),
    52.2,
    4.2,
    1500, // Altitude above WGS84 ellipsoid in meters
    0,    // in m/s
    50,   // in m/s
    0,    // 0..359
    0,    // deg/s Turn rate in the horizontal plane
    // 50,   // North velocity in m/s
    // 0,    // East velocity in m/s
    20,    // Height of geoid above WGS84 ellipsoid
    true,
    {
        0x123456,
        GATAS::AircraftCategory::LIGHT,
        GATAS::AddressType::ICAO,
        false,
        false
    },
};


// This test needs a bit more validation
TEST_CASE("Test filter below and above", "[single-file]")
{
    xSemaphoreTakeValue = pdTRUE;
    ADSBDecoder adsbDecoder{bus, mockConfig, ownshipState};
    adsbDecoder.on_receive(GATAS::AdapativeRadiusMsg{10'000'000});
    adsbDecoder.postConstruct();
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
        adsbDecoder.receiveBinary(data, 14);

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
        adsbDecoder.receiveBinary(data, 14);
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
        adsbDecoder.receiveBinary(data, line.size() - 1);
        if (test.received)
        {
            totalPlanes += 1;
        }
        adsbDecoder.on_receive(GATAS::Every5SecMsg());
    }
    printf("Total Planes below: %d\n", totalPlanes);
    REQUIRE(totalPlanes == 4599);
    infile.close();
}

TEST_CASE("Test heading and direction received aircraft", "[single-file]")
{
    xSemaphoreTakeValue = pdTRUE;
    ADSBDecoder adsbDecoder(bus, mockConfig, ownshipState);
    adsbDecoder.on_receive(GATAS::AdapativeRadiusMsg{1'000'000});
    adsbDecoder.postConstruct();
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
    adsbDecoder.receiveBinary(data, 14);
    CoreUtils::hexStrToByteArray("8d502cd15899965802eb001f31d8", data);
    adsbDecoder.receiveBinary(data, 14);
    CoreUtils::hexStrToByteArray("8d502cd19908c532903c9cced691", data);
    adsbDecoder.receiveBinary(data, 14);

    REQUIRE(test.received == true);
    REQUIRE(test.position.address == 0x502CD1);
    REQUIRE(test.position.addressType == GATAS::AddressType::ICAO);
    REQUIRE(test.position.dataSource == GATAS::DataSource::ADSB);
    REQUIRE(test.position.aircraftType == GATAS::AircraftCategory::UNKNOWN);
    // https://www.unavco.org/software/geodetic-utilities/geoid-height-calculator/geoid-height-calculator.html  for 52.3888,4.7209
    REQUIRE(test.position.ellipseHeight == /*9029*/ 9072); // geoid aprox 43m
    REQUIRE(test.position.groundSpeed == Catch::Approx(230.98).margin(0.1)); // in m/s
    REQUIRE(test.position.track == 25);                                     // ADSB data shows 25.94.. Should we use floats instead of int?
    REQUIRE(test.position.lat == Catch::Approx(52.3888).margin(0.005));
    REQUIRE(test.position.lon == Catch::Approx(4.7209).margin(0.005));
    REQUIRE(test.position.verticalSpeed == Catch::Approx(4.552).margin(0.001));

    REQUIRE(test.position.distanceFromOwn == Catch::Approx(32551).margin(1));
    REQUIRE(test.position.relNorthFromOwn(ownship) == Catch::Approx(32099).margin(1));
    REQUIRE(test.position.relEastFromOwn(ownship) == Catch::Approx(-5402).margin(1));
    // REQUIRE(test.position.bearingFromOwn == Catch::Approx(351).margin(0.5));
}

TEST_CASE("Test descending aircraft", "[single-file]")
{
    xSemaphoreTakeValue = pdTRUE;
    ADSBDecoder adsbDecoder{bus, mockConfig, ownshipState};
    adsbDecoder.on_receive(GATAS::AdapativeRadiusMsg{1'000'000});
    adsbDecoder.postConstruct();
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
    adsbDecoder.receiveBinary(data, 14);
    CoreUtils::hexStrToByteArray("8d407a0558178312d6eefaca54eb", data); // event
    adsbDecoder.receiveBinary(data, 14);
    CoreUtils::hexStrToByteArray("8d407a059908e102980c93715499", data);
    adsbDecoder.receiveBinary(data, 14);

    REQUIRE(test.position.verticalSpeed == Catch::Approx(-3.0).margin(0.001));
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
