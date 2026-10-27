
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#define private public

#include "../ace/aircrafttracker.hpp"
#include "ace/messages.hpp"
#include "ace/messagerouter.hpp"
#include "pico/time.h"
#include "mockconfig.h"

constexpr uint32_t OUT_OF_ADAPTIVE_RANGE = 200000;

static GATAS::OwnshipPositionInfo makeOwnship(float lat = 0.0f, float lon = 0.0f)
{
    GATAS::OwnshipPositionInfo ownship = {};
    ownship.lat = lat;
    ownship.lon = lon;
    return ownship;
}

class TestHandler
{
public:
    bool nextCalled = false;
    uint8_t callBacks = 0;
    void onNext(const GATAS::AircraftPositionInfo &) { nextCalled = true; callBacks += 1; }
};

class PositionCollector
{
public:
    etl::vector<GATAS::AircraftPositionInfo, 32> positions;

    void onNext(const GATAS::AircraftPositionInfo &position)
    {
        positions.push_back(position);
    }
};

TEST_CASE("AircraftTracker diagnostics contain one comma-separated row per aircraft", "[diagnostics]")
{
    CoreUtils::init();
    GATAS::ThreadSafeBus<8> bus;
    MockConfig config{bus};
    GATAS::OwnshipState ownshipState;
    ownshipState.init(CoreUtils::sharedSpinLock());
    auto ownship = makeOwnship();
    ownship.ellipseHeight = 1047;
    ownshipState.location.store(ownship);
    AircraftTracker tracker{bus, config, ownshipState};
    tracker.trackedAircraftMutex = xSemaphoreCreateMutex();
    time_us_Value = 0;

    auto diagnostics = [&]()
    {
        etl::string<4096> output;
        etl::string_stream stream(output);
        tracker.getData(stream, "");
        return output;
    };

    const auto empty = diagnostics();
    REQUIRE(empty.find("\"aircraft:aoa\":[]") != etl::string<4096>::npos);
    REQUIRE(empty.find("\"ownshipPressureSource\":\"None\"") != etl::string<4096>::npos);

    GATAS::AircraftPositionInfo aircraft;
    aircraft.address = 0x123ABC;
    aircraft.dataSource = GATAS::DataSource::ADSB;
    aircraft.distanceFromOwn = 42;
    aircraft.pressureAlt = -100;
    REQUIRE(tracker.trackedAircraft.insert(aircraft));

    aircraft.address = 0x000012;
    aircraft.dataSource = GATAS::DataSource::FLARM;
    aircraft.distanceFromOwn = 1500;
    aircraft.ellipseHeight = 1147;
    aircraft.pressureAlt = GATAS::INVALID_BARO_ALTITUDE;
    REQUIRE(tracker.trackedAircraft.insert(aircraft));

    aircraft.address = 0xABC123;
    aircraft.dataSource = GATAS::DataSource::OGN;
    aircraft.distanceFromOwn = 0;
    aircraft.pressureAlt = 0;
    REQUIRE(tracker.trackedAircraft.insert(aircraft));

    const auto output = diagnostics();
    REQUIRE(output.find("\"aircraft:aoa\":[") != etl::string<4096>::npos);
    REQUIRE(output.find("\"123ABC,ADSB,42,-100\"") != etl::string<4096>::npos);
    REQUIRE(output.find("\"000012,Flarm,1500,\"") != etl::string<4096>::npos);
    REQUIRE(output.find("\"ABC123,OGN,0,0\"") != etl::string<4096>::npos);
    REQUIRE(output.back() == '}');

    // FLARM has no reported pressure: resolve the +100 m height difference
    // against the ownship pressure reference, just as the GDL90 output does.
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::Internal,
                                         {1013.25f, GATAS::PressureSource::PressureSensor});
    const auto withPressure = diagnostics();
    REQUIRE(withPressure.find("\"ownshipPressureSource\":\"BMP280\"") != etl::string<4096>::npos);
    REQUIRE(withPressure.find("\"000012,Flarm,1500,100\"") != etl::string<4096>::npos);
    REQUIRE(withPressure.find("\"123ABC,ADSB,42,-100\"") != etl::string<4096>::npos);
    REQUIRE(withPressure.find("\"ABC123,OGN,0,0\"") != etl::string<4096>::npos);

    ownshipState.updateBarometricPressure(GATAS::BarometricSource::Internal,
                                         {GATAS::INVALID_PRESSSURE_HPA, GATAS::PressureSource::PressureSensor});
    const auto noPressure = diagnostics();
    REQUIRE(noPressure.find("\"000012,Flarm,1500,\"") != etl::string<4096>::npos);
    REQUIRE(noPressure.find("\"ownshipPressureSource\":\"None\"") != etl::string<4096>::npos);

    ownshipState.updateBarometricPressure(GATAS::BarometricSource::External,
                                         {1013.25f, GATAS::PressureSource::Calculated});
    REQUIRE(diagnostics().find("\"ownshipPressureSource\":\"Server\"") != etl::string<4096>::npos);
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::External,
                                         {1013.25f, GATAS::PressureSource::PressureSensor});
    REQUIRE(diagnostics().find("\"ownshipPressureSource\":\"Mobile\"") != etl::string<4096>::npos);
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::Internal,
                                         {1013.25f, GATAS::PressureSource::PressureSensor});
    REQUIRE(diagnostics().find("\"ownshipPressureSource\":\"BMP280\"") != etl::string<4096>::npos);
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::Internal,
                                         {GATAS::INVALID_PRESSSURE_HPA, GATAS::PressureSource::PressureSensor});
    REQUIRE(diagnostics().find("\"ownshipPressureSource\":\"Mobile\"") != etl::string<4096>::npos);
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::External,
                                         {GATAS::INVALID_PRESSSURE_HPA, GATAS::PressureSource::PressureSensor});
    REQUIRE(diagnostics().find("\"ownshipPressureSource\":\"Server\"") != etl::string<4096>::npos);
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::External,
                                         {GATAS::INVALID_PRESSSURE_HPA, GATAS::PressureSource::Calculated});
    REQUIRE(diagnostics().find("\"ownshipPressureSource\":\"None\"") != etl::string<4096>::npos);
}

TEST_CASE("TrackerData filters expired output before maintenance including clock wrap", "[expiry]")
{
    const uint32_t start = GENERATE(1'000'000U, UINT32_MAX - 5'000'000U);
    const uint32_t age = GENERATE(9'999'999U, 10'000'000U, 10'000'001U);
    const bool predictionEnabled = GENERATE(false, true);
    CAPTURE(start, age, predictionEnabled);
    time_us_Value = start;
    TrackerData<32, 10, 6> tracker;
    tracker.pathPrediction(predictionEnabled);
    const auto ownship = makeOwnship(52.0f, 4.0f);

    GATAS::AircraftPositionInfo position;
    position.address = 42;
    position.timestamp = start;
    position.lat = ownship.lat;
    position.lon = ownship.lon;
    position.distanceFromOwn = 0;
    position.groundSpeed = 50.0f;
    position.track = 90;
    REQUIRE(tracker.insert(position));

    time_us_Value = static_cast<uint64_t>(start) + age;
    const size_t expectedCount = age < 10'000'000U ? 1 : 0;
    PositionCollector collector;
    auto callback = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<PositionCollector, &PositionCollector::onNext>(collector);
    tracker.sendScheduled(callback, ownship);
    auto uplink = tracker.adslUplinkTrigger(ownship);
    REQUIRE(collector.positions.size() == expectedCount);
    REQUIRE(uplink.size() == expectedCount);
    if (expectedCount != 0)
    {
        const uint32_t expectedTimestamp = predictionEnabled ? static_cast<uint32_t>(time_us_Value) : start;
        REQUIRE(collector.positions[0].timestamp == expectedTimestamp);
        REQUIRE(uplink[0].timestamp == expectedTimestamp);
    }

    // Filtering must leave storage reclamation to the existing cleanup paths.
    REQUIRE(tracker.size() == 1);
    tracker.maintenance();
    REQUIRE(tracker.size() == expectedCount);
}

TEST_CASE("Predicted output expires by measurement age and resumes after a fresh update", "[expiry]")
{
    time_us_Value = 0;
    TrackerData<32, 10, 6> tracker;
    tracker.pathPrediction(true);
    const auto ownship = makeOwnship(52.0f, 4.0f);
    GATAS::AircraftPositionInfo position;
    position.address = 42;
    position.lat = ownship.lat;
    position.lon = ownship.lon;
    position.distanceFromOwn = 0;
    position.groundSpeed = 50.0f;
    position.track = 90;
    REQUIRE(tracker.insert(position));

    PositionCollector collector;
    auto callback = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<PositionCollector, &PositionCollector::onNext>(collector);
    time_us_Value = 9'000'000;
    tracker.sendScheduled(callback, ownship);
    REQUIRE(collector.positions.size() == 1);
    REQUIRE(collector.positions[0].timestamp == 9'000'000);
    REQUIRE(static_cast<float>(collector.positions[0].distanceFromOwn) == Catch::Approx(450.0f).margin(2.0f));
    auto uplink = tracker.adslUplinkTrigger(ownship);
    REQUIRE(uplink.size() == 1);
    REQUIRE(uplink[0].timestamp == 9'000'000);

    time_us_Value = 10'000'000;
    collector.positions.clear();
    tracker.sendScheduled(callback, ownship);
    REQUIRE(collector.positions.empty());
    REQUIRE(tracker.adslUplinkTrigger(ownship).empty());
    REQUIRE(tracker.size() == 1);

    time_us_Value = 11'000'000;
    position.timestamp = 11'000'000;
    position.lon = 4.008f;
    position.distanceFromOwn = 550;
    REQUIRE(tracker.insert(position));
    tracker.sendScheduled(callback, ownship);
    REQUIRE(collector.positions.size() == 1);
    REQUIRE(collector.positions[0].timestamp == position.timestamp);
    REQUIRE(collector.positions[0].lon == position.lon);
    uplink = tracker.adslUplinkTrigger(ownship);
    REQUIRE(uplink.size() == 1);
    REQUIRE(uplink[0].timestamp == position.timestamp);
}

TEST_CASE("Expired tracks do not consume scheduled output slots even without predictor history", "[expiry]")
{
    time_us_Value = 0;
    TrackerData<32, 10, 1> tracker;
    tracker.pathPrediction(true);
    // Ten expired tracks precede the live tracks in this map's iteration order.
    for (uint32_t address = 0; address < 10; ++address)
    {
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.distanceFromOwn = 100;
        REQUIRE(tracker.insert(position));
    }
    time_us_Value = 9'000'000;
    for (uint32_t address = 10; address < 12; ++address)
    {
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.timestamp = 9'000'000;
        position.distanceFromOwn = 1000;
        REQUIRE(tracker.insert(position));
    }

    time_us_Value = 10'000'000;
    PositionCollector collector;
    auto callback = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<PositionCollector, &PositionCollector::onNext>(collector);
    tracker.sendScheduled(callback, makeOwnship());
    REQUIRE(collector.positions.size() == 2);
    REQUIRE(collector.positions[0].address != collector.positions[1].address);
    for (const auto &position : collector.positions)
    {
        REQUIRE(position.address >= 10);
        // Both live tracks lack a predictor slot, so they retain their measurement time.
        REQUIRE(position.timestamp == 9'000'000);
    }
    REQUIRE(tracker.size() == 12);
}

TEST_CASE("ADS-L nearest selection skips expired tracks when more than ten are stored", "[expiry]")
{
    const uint32_t liveCount = GENERATE(0U, 1U, 9U, 10U, 11U, 20U);
    CAPTURE(liveCount);
    time_us_Value = 0;
    TrackerData<32, 10, 6> tracker;
    // Interleave close expired tracks with live tracks in the map's iteration order.
    for (uint32_t i = 0; i < 12; ++i)
    {
        GATAS::AircraftPositionInfo position;
        position.address = 2 * i;
        position.distanceFromOwn = 1;
        REQUIRE(tracker.insert(position));
    }
    time_us_Value = 9'000'000;
    for (uint32_t i = 0; i < liveCount; ++i)
    {
        GATAS::AircraftPositionInfo position;
        position.address = 2 * i + 1;
        position.timestamp = 9'000'000;
        position.distanceFromOwn = 1000 + 100 * i;
        REQUIRE(tracker.insert(position));
    }

    time_us_Value = 10'000'000;
    auto uplink = tracker.adslUplinkTrigger(makeOwnship());
    const uint32_t expectedCount = liveCount < 10 ? liveCount : 10;
    REQUIRE(uplink.size() == expectedCount);
    // Verify every expected address, independent of heap output order.
    for (uint32_t i = 0; i < expectedCount; ++i)
    {
        uint32_t occurrences = 0;
        for (const auto &position : uplink)
        {
            if (position.address == 2 * i + 1)
            {
                ++occurrences;
            }
        }
        REQUIRE(occurrences == 1);
    }
    REQUIRE(tracker.size() == 12 + liveCount);
}

TEST_CASE("Full storage still reclaims expired tracks on insertion", "[expiry]")
{
    time_us_Value = 0;
    TrackerData<32, 10, 6> tracker;
    for (uint32_t address = 0; address < 32; ++address)
    {
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.distanceFromOwn = 1000;
        REQUIRE(tracker.insert(position));
    }
    REQUIRE(tracker.full());

    time_us_Value = 10'000'000;
    GATAS::AircraftPositionInfo position;
    position.address = 100;
    position.timestamp = 10'000'000;
    position.distanceFromOwn = 1000;
    REQUIRE(tracker.insert(position));
    REQUIRE(tracker.size() == 1);
    REQUIRE(tracker.radius() == 75000);
    auto uplink = tracker.adslUplinkTrigger(makeOwnship());
    REQUIRE(uplink.size() == 1);
    REQUIRE(uplink[0].address == 100);
}

TEST_CASE("TrackerData ", "[single-file]")
{
    TrackerData<100, 2> trackedAircraft;
    REQUIRE(trackedAircraft.size() == 0);
}

TEST_CASE("TrackerData Insert within adaptiveRadius", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<100, 2> trackedAircraft;
    const auto ownship = makeOwnship();

    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.timestamp = 560'000;
    aircraftPosition.distanceFromOwn = 10'000;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    REQUIRE(trackedAircraft.size() == 1);

    SECTION("When adding, must stay unique")
    {
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.timestamp = 860'000;
        aircraftPosition.distanceFromOwn = 8000;
        REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
        REQUIRE(trackedAircraft.size() == 1);
    }

    SECTION("Same, but now Out of adaptiveRadious and should not have been added")
    {
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.timestamp = 860'000;
        aircraftPosition.address = 9999;
        aircraftPosition.distanceFromOwn = OUT_OF_ADAPTIVE_RANGE;
        REQUIRE(trackedAircraft.insert(aircraftPosition) == false);
        REQUIRE(trackedAircraft.size() == 1);
    }

    SECTION("A tracked aircraft is removed immediately when it leaves the adaptive radius")
    {
        REQUIRE(trackedAircraft.pathPredictor.contains(aircraftPosition.address));

        aircraftPosition.timestamp = 860'000;
        aircraftPosition.distanceFromOwn = OUT_OF_ADAPTIVE_RANGE;
        REQUIRE_FALSE(trackedAircraft.insert(aircraftPosition));
        REQUIRE(trackedAircraft.size() == 0);
        REQUIRE_FALSE(trackedAircraft.pathPredictor.contains(aircraftPosition.address));
    }

    SECTION("An older out-of-range position cannot remove a newer tracked aircraft")
    {
        aircraftPosition.timestamp = 559'999;
        aircraftPosition.distanceFromOwn = OUT_OF_ADAPTIVE_RANGE;
        REQUIRE_FALSE(trackedAircraft.insert(aircraftPosition));
        REQUIRE(trackedAircraft.size() == 1);
        REQUIRE(trackedAircraft.pathPredictor.contains(aircraftPosition.address));
    }

    SECTION("next Called, first one.. but not next one ")
    {
        TestHandler testHandler;
        time_us_Value = 120'000;
        trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);

        REQUIRE(testHandler.nextCalled);

        time_us_Value = 920'000;
        testHandler.nextCalled = false;
        trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);
        REQUIRE_FALSE(testHandler.nextCalled);

    }

    SECTION("next called with timeslice")
    {
        time_us_Value = 900'000;
        TestHandler testHandler;
        trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);

        REQUIRE(testHandler.nextCalled);

        // Would not call again with the same time as send time is updated
        testHandler.nextCalled = false;
        trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);

        time_us_Value = time_us_Value + 250'000;
        trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);
    }


    SECTION("next called runs stale")
    {
        time_us_Value = aircraftPosition.timestamp + 11'000'000;
        trackedAircraft.maintenance();
        REQUIRE(trackedAircraft.size() == 0);
    }

    SECTION("Additional aircraft added ")
    {
        class TestHandler
        {
        public:
            int callBacks = 0;
            void onNext(const GATAS::AircraftPositionInfo &position)
            {
                if (callBacks == 0)
                {
                    REQUIRE(position.address == 0);
                }
                if (callBacks == 1)
                {
                    REQUIRE(position.address == 1);
                }
                callBacks += 1;
            }
        };

        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.timestamp = 570'000;
        aircraftPosition.distanceFromOwn = 10000;
        aircraftPosition.address = 1;
        REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
        REQUIRE(trackedAircraft.size() == 2);
        int callBacks = 0;
        time_us_Value = 1'500'000;
        TestHandler testHandler;
        trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);
        REQUIRE(testHandler.callBacks == 1);
    }
}

TEST_CASE("TrackerData Insert many and re-calculate adaptiveRadius ", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<100, 2> trackedAircraft;

    int i = 0;
    for (; i < 100; i++)
    {
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.distanceFromOwn = 10000 + 100 * i;
        aircraftPosition.address = i;
        REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    }
    REQUIRE(trackedAircraft.size() == 100);

    SECTION("A distant newcomer is rejected after cleanup reduces the radius")
    {
        REQUIRE(trackedAircraft.radius() == 75000);
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.distanceFromOwn = 10000 + 100 * i;
        aircraftPosition.address = i;
        REQUIRE_FALSE(trackedAircraft.insert(aircraftPosition));
        REQUIRE(trackedAircraft.size() == 90);
        // Ten farthest tracks are removed; the last removed distance is the cutoff.
        REQUIRE(trackedAircraft.radius() == 19000);
    }

    SECTION("Must recalculate adaptive radius when planes added within")
    {
        auto radius = trackedAircraft.radius();
        for (int i = 125; i < 175; i++)
        {
            GATAS::AircraftPositionInfo aircraftPosition;
            aircraftPosition.distanceFromOwn = i * 10;
            aircraftPosition.address = i;
            // Ensure that new planes are always added
            REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
        }
        trackedAircraft.maintenance();
        // Check radious decreased
        REQUIRE(trackedAircraft.radius() < radius);

        SECTION("Keep calling to max radius size should increase to ADAPTIVE_RADIUS_MAX")
        {
            time_us_Value = 12'000'000;
            for (i = 0; i < 100; i++)
            {
                trackedAircraft.maintenance();
            }
            REQUIRE(trackedAircraft.radius() == trackedAircraft.ADAPTIVE_RADIUS_MAX);
        }
    }
}

TEST_CASE("Newcomer admission uses the radius after full-storage cleanup", "[capacity]")
{
    const uint32_t candidateDistance = GENERATE(28'999U, 29'000U, 29'001U, 70'000U);
    const bool storedTracksExpired = GENERATE(false, true);
    CAPTURE(candidateDistance, storedTracksExpired);
    time_us_Value = 0;
    TrackerData<32, 10, 6> tracker;
    for (uint32_t address = 0; address < 32; ++address)
    {
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.distanceFromOwn = (address + 1) * 1000;
        REQUIRE(tracker.insert(position));
    }
    REQUIRE(tracker.full());

    time_us_Value = storedTracksExpired ? 10'000'000 : 1'000'000;
    GATAS::AircraftPositionInfo newcomer;
    newcomer.address = 100;
    newcomer.timestamp = static_cast<uint32_t>(time_us_Value);
    newcomer.distanceFromOwn = candidateDistance;
    const bool expectedAccepted = storedTracksExpired || candidateDistance <= 29'000;
    REQUIRE(tracker.insert(newcomer) == expectedAccepted);
    REQUIRE(tracker.radius() == (storedTracksExpired ? 75'000 : 29'000));
    REQUIRE(tracker.size() == (storedTracksExpired ? 0 : 28) + (expectedAccepted ? 1 : 0));

    uint32_t candidateOccurrences = 0;
    tracker.forEachPosition([&](const GATAS::AircraftPositionInfo &position)
    {
        REQUIRE(position.distanceFromOwn <= tracker.radius());
        if (position.address == newcomer.address)
        {
            ++candidateOccurrences;
        }
    });
    REQUIRE(candidateOccurrences == (expectedAccepted ? 1 : 0));
    if (!expectedAccepted)
    {
        REQUIRE_FALSE(tracker.pathPredictor.contains(newcomer.address));
        // Cleanup still leaves space for the next, nearer arrival.
        newcomer.distanceFromOwn = 500;
        REQUIRE(tracker.insert(newcomer));
        REQUIRE(tracker.size() == 29);
    }
}

TEST_CASE("Capacity cleanup retains clustered aircraft and their predictor state", "[capacity]")
{
    const uint32_t baseDistance = GENERATE(100U, 10'000U);
    const uint32_t spacing = GENERATE(0U, 1U);
    const bool reverseInsertion = GENERATE(false, true);
    CAPTURE(baseDistance, spacing, reverseInsertion);
    time_us_Value = 0;
    TrackerData<32, 10, 32> tracker;
    for (uint32_t i = 0; i < 32; ++i)
    {
        GATAS::AircraftPositionInfo position;
        position.address = reverseInsertion ? 31 - i : i;
        position.distanceFromOwn = baseDistance + spacing * position.address;
        REQUIRE(tracker.insert(position));
    }

    GATAS::AircraftPositionInfo newcomer;
    newcomer.address = 100;
    newcomer.distanceFromOwn = 0;
    REQUIRE(tracker.insert(newcomer));
    REQUIRE(tracker.size() == 29);
    REQUIRE(tracker.pathPredictor.size() == 29);
    for (uint32_t address = 0; address < 32; ++address)
    {
        uint32_t occurrences = 0;
        tracker.forEachPosition([&](const GATAS::AircraftPositionInfo &position)
        {
            if (position.address == address)
            {
                ++occurrences;
            }
        });
        REQUIRE(occurrences == (address < 28 ? 1 : 0));
        REQUIRE(tracker.pathPredictor.contains(address) == (address < 28));
    }
    REQUIRE(tracker.pathPredictor.contains(newcomer.address));

    // A retained aircraft must not be removed on its next unchanged-distance update.
    time_us_Value = 1;
    for (uint32_t address = 0; address < 28; ++address)
    {
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.timestamp = 1;
        position.distanceFromOwn = baseDistance + spacing * address;
        REQUIRE(position.distanceFromOwn <= tracker.radius());
        REQUIRE(tracker.insert(position));
    }
    REQUIRE(tracker.size() == 29);
}

TEST_CASE("Repeated capacity pressure removes only four tracks per cleanup", "[capacity]")
{
    time_us_Value = 0;
    TrackerData<32, 10, 6> tracker;
    for (uint32_t address = 0; address < 64; ++address)
    {
        const auto previousSize = tracker.size();
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.distanceFromOwn = 100;
        REQUIRE(tracker.insert(position));
        REQUIRE(tracker.size() == (previousSize == 32 ? 29 : previousSize + 1));
        REQUIRE(tracker.radius() >= 100);
    }

    time_us_Value = 10'000'000;
    tracker.maintenance();
    REQUIRE(tracker.size() == 0);
    REQUIRE(tracker.radius() == 1100);
}

TEST_CASE("Small tracker capacity cleanup still leaves room for a new aircraft", "[capacity]")
{
    time_us_Value = 0;
    TrackerData<4, 2> tracker;
    for (uint32_t address = 0; address < 4; ++address)
    {
        GATAS::AircraftPositionInfo position;
        position.address = address;
        position.distanceFromOwn = 100;
        REQUIRE(tracker.insert(position));
    }
    GATAS::AircraftPositionInfo newcomer;
    newcomer.address = 10;
    newcomer.distanceFromOwn = 0;
    REQUIRE(tracker.insert(newcomer));
    REQUIRE(tracker.size() == 4);
    REQUIRE_FALSE(tracker.pathPredictor.contains(3));
    REQUIRE(tracker.pathPredictor.contains(0));
    REQUIRE(tracker.pathPredictor.contains(1));
    REQUIRE(tracker.pathPredictor.contains(2));
    REQUIRE(tracker.pathPredictor.contains(10));
}

TEST_CASE("sendScheduled distributes 4 aircraft across 2 timeslices", "[single-file]")
{
    // With TIMESLICES=2 and 4 aircraft: maxPerRound = ceil(4/2) = 2.
    // First sendScheduled fires 2, sets their sendTime = currentTime + HEARTBEAT_TIME.
    // Second sendScheduled 500ms later fires the remaining 2 (still at sendTime=0).
    time_us_Value = 0;
    TrackerData<100, 2> trackedAircraft;
    const auto ownship = makeOwnship();

    for (uint32_t i = 0; i < 4; ++i)
    {
        GATAS::AircraftPositionInfo ac;
        ac.address = i;
        ac.timestamp = 0;
        ac.distanceFromOwn = 5000;
        trackedAircraft.insert(ac);
    }
    REQUIRE(trackedAircraft.size() == 4);

    TestHandler testHandler;
    auto delegate = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler);

    // First slot at t=0: maxPerRound = ceil(4/2) = 2, fires 2
    trackedAircraft.sendScheduled(delegate, ownship);
    REQUIRE(testHandler.callBacks == 2);

    // Second slot at t=600ms: remaining 2 still have sendTime=0, fires 2
    time_us_Value = 600'000;
    trackedAircraft.sendScheduled(delegate, ownship);
    REQUIRE(testHandler.callBacks == 4);

    // Calling again at the same time: all sendTimes are in the future, nothing fires
    trackedAircraft.sendScheduled(delegate, ownship);
    REQUIRE(testHandler.callBacks == 4);

    //Time advances
    time_us_Value = time_us_Value + 500'000;
    trackedAircraft.sendScheduled(delegate, ownship);
    REQUIRE(testHandler.callBacks == 6);
}

TEST_CASE("Position updates preserve scheduled output for every aircraft", "[scheduling]")
{
    const uint32_t start = GENERATE(1'000'000U, UINT32_MAX - 500'000U);
    const uint32_t updateEveryTicks = GENERATE(1U, 10U); // 10 Hz and normal 1 Hz input.
    const bool updateAllAircraft = GENERATE(false, true);
    CAPTURE(start, updateEveryTicks, updateAllAircraft);
    time_us_Value = start;
    TrackerData<32, 10, 6> tracker;
    const auto ownship = makeOwnship();
    etl::array<GATAS::AircraftPositionInfo, 10> latest;
    etl::array<uint32_t, 10> counts = {};
    etl::array<uint32_t, 10> lastSentTime = {};
    for (uint32_t address = 0; address < latest.size(); ++address)
    {
        latest[address].address = address;
        latest[address].timestamp = start;
        latest[address].distanceFromOwn = 1000;
        latest[address].ellipseHeight = 1000;
        REQUIRE(tracker.insert(latest[address]));
    }

    PositionCollector collector;
    auto callback = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<PositionCollector, &PositionCollector::onNext>(collector);
    uint32_t frequentAddress = 0;
    for (uint32_t tick = 0; tick < 30; ++tick)
    {
        time_us_Value = static_cast<uint64_t>(start) + tick * 100'000U;
        if (tick != 0 && tick % updateEveryTicks == 0)
        {
            for (auto &position : latest)
            {
                if (updateAllAircraft || position.address == frequentAddress)
                {
                    position.timestamp = static_cast<uint32_t>(time_us_Value);
                    position.ellipseHeight = 1000 + tick;
                    REQUIRE(tracker.insert(position));
                }
            }
        }

        collector.positions.clear();
        tracker.sendScheduled(callback, ownship);
        // Ten tracks / ten timeslices permits exactly one output per round.
        REQUIRE(collector.positions.size() == 1);
        const auto &output = collector.positions[0];
        REQUIRE(output.address < latest.size());
        if (tick == 0)
        {
            // Exercise whichever aircraft is first, without assuming map order.
            frequentAddress = output.address;
        }
        REQUIRE(output.timestamp == latest[output.address].timestamp);
        REQUIRE(output.ellipseHeight == latest[output.address].ellipseHeight);
        if (counts[output.address] != 0)
        {
            const uint32_t interval = static_cast<uint32_t>(time_us_Value) - lastSentTime[output.address];
            REQUIRE(interval == 1'000'000);
        }
        lastSentTime[output.address] = static_cast<uint32_t>(time_us_Value);
        ++counts[output.address];

        if ((tick + 1) % 10 == 0)
        {
            for (const auto count : counts)
            {
                REQUIRE(count == (tick + 1) / 10);
            }
        }
    }
}

TEST_CASE("Should update data", "[single-file]")
{
    class TestHandler
    {
    public:
        int callBacks = 0;
        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            REQUIRE(position.distanceFromOwn == 20000);
            callBacks += 1;
        }
    } testHandler;
    TrackerData<100, 4> trackedAircraft;
    const auto ownship = makeOwnship();

    time_us_Value = 1;
    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.timestamp = 570'000;
    aircraftPosition.distanceFromOwn = 10000;
    aircraftPosition.address = 1;

    // Insert aircraft
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    REQUIRE(trackedAircraft.size() == 1);

    // Insert same aircraft with other data tested by distanceFromOwn
    aircraftPosition.timestamp = 1'570'000;
    aircraftPosition.distanceFromOwn = 20000;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    REQUIRE(trackedAircraft.size() == 1);

    // Validate if the queue was updated
    trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(testHandler), ownship);

    REQUIRE(testHandler.callBacks == 1);
}

TEST_CASE("Updating an existing aircraft must not trigger full-buffer cleanup", "[single-file]")
{
    TrackerData<4, 2> trackedAircraft;

    for (uint32_t i = 0; i < 4; ++i)
    {
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.address = i;
        aircraftPosition.distanceFromOwn = 1000 + 1000 * i;
        REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    }

    REQUIRE(trackedAircraft.size() == 4);
    REQUIRE(trackedAircraft.radius() == 75000);

    GATAS::AircraftPositionInfo update;
    update.address = 0;
    update.distanceFromOwn = 1500;
    REQUIRE(trackedAircraft.insert(update) == true);

    REQUIRE(trackedAircraft.size() == 4);
    REQUIRE(trackedAircraft.radius() == 75000);
}

TEST_CASE("Path predictor keeps the closest tracked aircraft when full", "[single-file]")
{
    TrackerData<8, 2, 6> trackedAircraft;
    trackedAircraft.pathPrediction(true);

    for (uint32_t i = 0; i < 6; ++i)
    {
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.address = 10 + i;
        aircraftPosition.timestamp = 1'000'000 + i;
        aircraftPosition.distanceFromOwn = 1000 * (i + 1);
        REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
        REQUIRE(trackedAircraft.pathPredictor.contains(aircraftPosition.address));
    }

    REQUIRE(trackedAircraft.pathPredictor.size() == 6);
    REQUIRE(trackedAircraft.pathPredictor.contains(10));
    REQUIRE(trackedAircraft.pathPredictor.contains(15));

    GATAS::AircraftPositionInfo closerNew;
    closerNew.address = 20;
    closerNew.timestamp = 2'000'000;
    closerNew.distanceFromOwn = 1500;
    REQUIRE(trackedAircraft.insert(closerNew) == true);

    REQUIRE_FALSE(trackedAircraft.pathPredictor.contains(15));
    REQUIRE(trackedAircraft.pathPredictor.contains(20));
    REQUIRE(trackedAircraft.pathPredictor.contains(10));

    GATAS::AircraftPositionInfo fartherNew;
    fartherNew.address = 21;
    fartherNew.timestamp = 3'000'000;
    fartherNew.distanceFromOwn = 9000;
    REQUIRE(trackedAircraft.insert(fartherNew) == true);

    REQUIRE_FALSE(trackedAircraft.pathPredictor.contains(21));
    REQUIRE(trackedAircraft.pathPredictor.contains(20));
    REQUIRE(trackedAircraft.pathPredictor.contains(10));
}

TEST_CASE("Path predictor tracks all aircraft when predictor size matches tracker size", "[single-file]")
{
    TrackerData<10, 2, 10> trackedAircraft;
    trackedAircraft.pathPrediction(true);

    for (uint32_t i = 0; i < 8; ++i)
    {
        GATAS::AircraftPositionInfo aircraftPosition;
        aircraftPosition.address = 30 + i;
        aircraftPosition.timestamp = 1'000'000 + i;
        aircraftPosition.distanceFromOwn = 1000 * (i + 1);
        REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
        REQUIRE(trackedAircraft.pathPredictor.contains(aircraftPosition.address));
    }

    REQUIRE(trackedAircraft.pathPredictor.size() == 8);

    GATAS::AircraftPositionInfo fartherNew;
    fartherNew.address = 50;
    fartherNew.timestamp = 2'000'000;
    fartherNew.distanceFromOwn = 9'000;
    REQUIRE(trackedAircraft.insert(fartherNew) == true);

    REQUIRE(trackedAircraft.pathPredictor.contains(30));
    REQUIRE(trackedAircraft.pathPredictor.contains(37));
    REQUIRE(trackedAircraft.pathPredictor.contains(50));
}

TEST_CASE("Path predictor derives turn behavior from recent history", "[single-file]")
{
    TrackerData<10, 2, 10> trackedAircraft;
    trackedAircraft.pathPrediction(true);

    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.address = 60;
    aircraftPosition.dataSource = GATAS::DataSource::OGN;
    aircraftPosition.lat = 52.0f;
    aircraftPosition.lon = 4.0f;
    aircraftPosition.ellipseHeight = 1000;
    aircraftPosition.groundSpeed = 50.0f;
    aircraftPosition.distanceFromOwn = 1000;
    aircraftPosition.timestamp = 0;
    aircraftPosition.track = 350;
    aircraftPosition.hTurnRate = NAN;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    aircraftPosition.timestamp = 1'000'000;
    aircraftPosition.lat = 52.00045f;
    aircraftPosition.track = 0;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    aircraftPosition.timestamp = 2'000'000;
    aircraftPosition.lat = 52.00089f;
    aircraftPosition.lon = 4.00008f;
    aircraftPosition.track = 10;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    GATAS::AircraftPositionInfo predicted = trackedAircraft.pathPredictor.extrapolatedPos(3'000'000, aircraftPosition);
    REQUIRE(predicted.track == 20);
}

TEST_CASE("adslUplinkTrigger returns predicted output positions when path prediction is enabled", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<10, 2, 10> trackedAircraft;
    trackedAircraft.pathPrediction(true);

    auto ownship = makeOwnship(52.0f, 4.0f);
    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.address = 70;
    aircraftPosition.timestamp = 0;
    aircraftPosition.lat = 52.0f;
    aircraftPosition.lon = 4.0f;
    aircraftPosition.ellipseHeight = 1000;
    aircraftPosition.groundSpeed = 50.0f;
    aircraftPosition.track = 90;
    aircraftPosition.distanceFromOwn = 0;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    time_us_Value = 2'000'000;
    auto closest = trackedAircraft.adslUplinkTrigger(ownship);
    REQUIRE(closest.size() == 1);
    REQUIRE(closest[0].timestamp == 2'000'000);
    REQUIRE(static_cast<float>(closest[0].distanceFromOwn) == Catch::Approx(100.0f).margin(2.0f));

    auto rel = CoreUtils::northEastDistance(52.0f, 4.0f, closest[0].lat, closest[0].lon);
    REQUIRE(rel.east == Catch::Approx(100.0f).margin(1.0f));
}

TEST_CASE("Path predictor keeps history while prediction output is disabled", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<10, 2, 10> trackedAircraft;
    auto ownship = makeOwnship(52.0f, 4.0f);

    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.address = 71;
    aircraftPosition.timestamp = 0;
    aircraftPosition.lat = 52.0f;
    aircraftPosition.lon = 4.0f;
    aircraftPosition.groundSpeed = 50.0f;
    aircraftPosition.track = 90;
    aircraftPosition.distanceFromOwn = 0;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    REQUIRE(trackedAircraft.pathPredictor.contains(aircraftPosition.address));

    time_us_Value = 2'000'000;
    auto closest = trackedAircraft.adslUplinkTrigger(ownship);
    REQUIRE(closest.size() == 1);
    REQUIRE(closest[0].timestamp == 0);

    trackedAircraft.pathPrediction(true);
    closest = trackedAircraft.adslUplinkTrigger(ownship);
    REQUIRE(closest.size() == 1);
    REQUIRE(closest[0].timestamp == 2'000'000);
    REQUIRE(static_cast<float>(closest[0].distanceFromOwn) == Catch::Approx(100.0f).margin(2.0f));
}

TEST_CASE("Predicted output does not mutate stored original distance", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<10, 2, 10> trackedAircraft;
    trackedAircraft.pathPrediction(true);

    auto ownship = makeOwnship(52.0f, 4.0f);
    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.address = 72;
    aircraftPosition.timestamp = 0;
    aircraftPosition.lat = 52.0f;
    aircraftPosition.lon = 4.0f;
    aircraftPosition.groundSpeed = 50.0f;
    aircraftPosition.track = 90;
    aircraftPosition.distanceFromOwn = 0;
    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    time_us_Value = 2'000'000;
    auto closest = trackedAircraft.adslUplinkTrigger(ownship);
    REQUIRE(closest.size() == 1);
    REQUIRE(static_cast<float>(closest[0].distanceFromOwn) == Catch::Approx(100.0f).margin(2.0f));

    auto it = trackedAircraft.trackedAircraft.find(72);
    REQUIRE(it != trackedAircraft.trackedAircraft.end());
    REQUIRE(it->second.position.timestamp == 0);
    REQUIRE(it->second.position.distanceFromOwn == 0);
}

TEST_CASE("TrackerData rejects out-of-order position updates", "[single-file]")
{
    time_us_Value = 2'000'000;
    TrackerData<10, 2, 10> trackedAircraft;
    trackedAircraft.pathPrediction(true);

    GATAS::AircraftPositionInfo latest;
    latest.address = 73;
    latest.timestamp = 2'000'000;
    latest.lat = 52.0f;
    latest.lon = 4.0f;
    latest.groundSpeed = 50.0f;
    latest.track = 90;
    latest.distanceFromOwn = 1'000;
    REQUIRE(trackedAircraft.insert(latest));

    time_us_Value = 2'500'000;
    GATAS::AircraftPositionInfo older = latest;
    older.timestamp = 1'000'000;
    older.lat = 51.0f;
    older.lon = 3.0f;
    older.distanceFromOwn = 2'000;
    REQUIRE_FALSE(trackedAircraft.insert(older));

    auto stored = trackedAircraft.trackedAircraft.find(latest.address);
    REQUIRE(stored != trackedAircraft.trackedAircraft.end());
    REQUIRE(stored->second.position.timestamp == latest.timestamp);
    REQUIRE(stored->second.position.lat == latest.lat);
    REQUIRE(stored->second.position.lon == latest.lon);
    REQUIRE(stored->second.position.distanceFromOwn == latest.distanceFromOwn);
    REQUIRE(stored->second.sendTime == 2'000'000);
}

TEST_CASE("Radio priority is checked before out-of-range deletion", "[radio-priority]")
{
    const uint32_t start = GENERATE(2'000'000U, UINT32_MAX - 2'000'000U);
    const uint32_t age = GENERATE(3'999'999U, 4'000'000U, 4'000'001U);
    const auto source = GENERATE(GATAS::DataSource::ADSB, GATAS::DataSource::MLAT, GATAS::DataSource::OGN);
    const int sampleOrder = GENERATE(-1, 0, 1);
    CAPTURE(start, age, source, sampleOrder);
    time_us_Value = start;
    TrackerData<32, 10, 6> tracker;
    tracker.pathPrediction(true);

    GATAS::AircraftPositionInfo radio;
    radio.address = 42;
    radio.timestamp = start;
    radio.distanceFromOwn = 74'990;
    radio.dataSource = GATAS::DataSource::OGN;
    radio.lat = 52.0f;
    radio.lon = 4.0f;
    radio.groundSpeed = 50.0f;
    radio.hTurnRate = 2.0f;
    REQUIRE(tracker.insert(radio));

    // Put the track on its next heartbeat deadline before testing rejection.
    TestHandler handler;
    auto callback = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<TestHandler, &TestHandler::onNext>(handler);
    tracker.sendScheduled(callback, makeOwnship());
    REQUIRE(handler.callBacks == 1);
    const auto deadline = tracker.trackedAircraft.find(radio.address)->second.sendTime;

    time_us_Value = static_cast<uint64_t>(start) + age;
    const auto predictionBefore = tracker.pathPredictor.extrapolatedPos(static_cast<uint32_t>(time_us_Value), radio);
    auto incoming = radio;
    incoming.timestamp = sampleOrder < 0 ? start - 1 : (sampleOrder == 0 ? start : static_cast<uint32_t>(time_us_Value));
    incoming.distanceFromOwn = 75'010;
    incoming.dataSource = source;
    incoming.hTurnRate = -10.0f;
    REQUIRE_FALSE(tracker.insert(incoming));

    const bool shouldRetain = sampleOrder < 0 || (source != GATAS::DataSource::OGN && age < 4'000'000U);
    REQUIRE(tracker.size() == (shouldRetain ? 1 : 0));
    REQUIRE(tracker.pathPredictor.contains(radio.address) == shouldRetain);
    if (shouldRetain)
    {
        auto stored = tracker.trackedAircraft.find(radio.address);
        REQUIRE(stored != tracker.trackedAircraft.end());
        REQUIRE(stored->second.sendTime == deadline);
        REQUIRE(stored->second.position.timestamp == radio.timestamp);
        REQUIRE(stored->second.position.dataSource == radio.dataSource);
        REQUIRE(stored->second.position.distanceFromOwn == radio.distanceFromOwn);
        REQUIRE(stored->second.position.lat == radio.lat);
        REQUIRE(stored->second.position.lon == radio.lon);
        const auto predictionAfter = tracker.pathPredictor.extrapolatedPos(static_cast<uint32_t>(time_us_Value), radio);
        REQUIRE(predictionAfter.lat == predictionBefore.lat);
        REQUIRE(predictionAfter.lon == predictionBefore.lon);
        REQUIRE(predictionAfter.track == predictionBefore.track);
        REQUIRE(predictionAfter.hTurnRate == predictionBefore.hTurnRate);
    }
}

TEST_CASE("Radio priority: fresh RADIO, MLAT incoming - should NOT update", "[single-file]")
{
    TrackerData<100, 4> trackedAircraft;
    time_us_Value = 0;
    const auto ownship = makeOwnship();

    // Insert radio data at t=0
    GATAS::AircraftPositionInfo radioPosition;
    radioPosition.address = 42;
    radioPosition.timestamp = 0;
    radioPosition.distanceFromOwn = 5000;
    radioPosition.dataSource = GATAS::DataSource::OGN;
    REQUIRE(trackedAircraft.insert(radioPosition) == true);

    // At t=2000000us, incoming MLAT data arrives
    // Radio is 2000000us old, well within priority timeout (4000000us)
    time_us_Value = 2000000;
    GATAS::AircraftPositionInfo mlatPosition;
    mlatPosition.address = 42;
    mlatPosition.timestamp = 2000000;
    mlatPosition.distanceFromOwn = 5100;
    mlatPosition.dataSource = GATAS::DataSource::MLAT;

    REQUIRE(trackedAircraft.insert(mlatPosition) == false);
    REQUIRE(trackedAircraft.size() == 1);
    REQUIRE(trackedAircraft.trackedAircraft.find(radioPosition.address)->second.sendTime == 0);

    // Verify radio data was NOT replaced
    class VerifyNotUpdatedHandler
    {
    public:
        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            REQUIRE(position.distanceFromOwn == 5000);  // Should be original radio data
            REQUIRE(position.dataSource == GATAS::DataSource::OGN);
        }
    } handler;
    time_us_Value = 2000100;
    trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<VerifyNotUpdatedHandler, &VerifyNotUpdatedHandler::onNext>(handler), ownship);
}

TEST_CASE("Radio priority: expired RADIO, MLAT incoming - should UPDATE", "[single-file]")
{
    TrackerData<100, 4> trackedAircraft;
    time_us_Value = 0;
    const auto ownship = makeOwnship();

    // Insert radio data at t=0
    GATAS::AircraftPositionInfo radioPosition;
    radioPosition.address = 42;
    radioPosition.timestamp = 0;
    radioPosition.distanceFromOwn = 5000;
    radioPosition.dataSource = GATAS::DataSource::OGN;
    REQUIRE(trackedAircraft.insert(radioPosition) == true);

    // At t=5000000us, incoming MLAT data arrives
    // Radio is 5000000us old, should exceed priority timeout
    time_us_Value = 5000000;
    GATAS::AircraftPositionInfo mlatPosition;
    mlatPosition.address = 42;
    mlatPosition.timestamp = 5000000;
    mlatPosition.distanceFromOwn = 5100;
    mlatPosition.dataSource = GATAS::DataSource::MLAT;

    REQUIRE(trackedAircraft.insert(mlatPosition) == true);
    REQUIRE(trackedAircraft.size() == 1);

    // Verify MLAT data WAS inserted
    class VerifyUpdatedHandler
    {
    public:
        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            REQUIRE(position.distanceFromOwn == 5100);  // Should be updated MLAT data
            REQUIRE(position.dataSource == GATAS::DataSource::MLAT);
        }
    } handler;
    time_us_Value = 5000100;
    trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<VerifyUpdatedHandler, &VerifyUpdatedHandler::onNext>(handler), ownship);
}

TEST_CASE("Radio priority: fresh RADIO, ADSB incoming - should NOT update", "[single-file]")
{
    TrackerData<100, 4> trackedAircraft;
    time_us_Value = 0;
    const auto ownship = makeOwnship();

    GATAS::AircraftPositionInfo radioPosition;
    radioPosition.address = 42;
    radioPosition.timestamp = 0;
    radioPosition.distanceFromOwn = 5000;
    radioPosition.dataSource = GATAS::DataSource::OGN;
    REQUIRE(trackedAircraft.insert(radioPosition));

    time_us_Value = 2000000;
    GATAS::AircraftPositionInfo adsbPosition;
    adsbPosition.address = 42;
    adsbPosition.timestamp = 2000000;
    adsbPosition.distanceFromOwn = 5100;
    adsbPosition.dataSource = GATAS::DataSource::ADSB;

    REQUIRE_FALSE(trackedAircraft.insert(adsbPosition));

    class VerifyNotUpdatedHandler
    {
    public:
        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            REQUIRE(position.distanceFromOwn == 5000);
            REQUIRE(position.dataSource == GATAS::DataSource::OGN);
        }
    } handler;
    time_us_Value = 2000100;
    trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<VerifyNotUpdatedHandler, &VerifyNotUpdatedHandler::onNext>(handler), ownship);
}

TEST_CASE("Data source prefix preserves fixed callsign length", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<100, 4> trackedAircraft;
    trackedAircraft.prefixEnabled(true);
    const auto ownship = makeOwnship();

    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.distanceFromOwn = 5000;
    aircraftPosition.dataSource = GATAS::DataSource::FLARM;
    aircraftPosition.callSign = "PH-ABCDEFGHIJKL";

    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    class VerifyPrefixLengthHandler
    {
    public:
        uint8_t callCount = 0;

        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            ++callCount;
            REQUIRE(position.callSign == "flPH-ABCDEFG");
        }
    } handler;

    trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<VerifyPrefixLengthHandler, &VerifyPrefixLengthHandler::onNext>(handler), ownship);
    REQUIRE(handler.callCount == 1);
}

TEST_CASE("Data source prefix does create callsigns for empty values", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<100, 4> trackedAircraft;
    trackedAircraft.prefixEnabled(true);
    const auto ownship = makeOwnship();

    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.distanceFromOwn = 5000;
    aircraftPosition.dataSource = GATAS::DataSource::ADSB;

    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    class VerifyEmptyHandler
    {
    public:
        uint8_t callCount = 0;

        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            ++callCount;
            REQUIRE(position.callSign == "ab");
        }
    } handler;

    trackedAircraft.sendScheduled(etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<VerifyEmptyHandler, &VerifyEmptyHandler::onNext>(handler), ownship);
    REQUIRE(handler.callCount == 1);
}

TEST_CASE("Squawk display replaces callsign when squawk is known", "[single-file]")
{
    time_us_Value = 0;
    TrackerData<100, 4> trackedAircraft;
    trackedAircraft.prefixEnabled(true);
    trackedAircraft.showSquawk(true);
    const auto ownship = makeOwnship();

    GATAS::AircraftPositionInfo aircraftPosition;
    aircraftPosition.distanceFromOwn = 5000;
    aircraftPosition.dataSource = GATAS::DataSource::ADSB;
    aircraftPosition.callSign = "PH-ABC";
    aircraftPosition.squawk = 42;

    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);

    class VerifySquawkHandler
    {
    public:
        uint8_t callCount = 0;

        void onNext(const GATAS::AircraftPositionInfo &position)
        {
            REQUIRE(position.callSign == "ab0042");
            callCount += 1;
        }
    } handler;

    auto callback = etl::delegate<void(const GATAS::AircraftPositionInfo &)>::create<VerifySquawkHandler, &VerifySquawkHandler::onNext>(handler);
    trackedAircraft.sendScheduled(callback, ownship);
    REQUIRE(handler.callCount == 1);

    aircraftPosition.timestamp = 1;
    aircraftPosition.callSign = "PH-DEF";
    time_us_Value = 1'100'000;

    REQUIRE(trackedAircraft.insert(aircraftPosition) == true);
    trackedAircraft.sendScheduled(callback, ownship);
    REQUIRE(handler.callCount == 2);
}
