#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../ace/aircraftpathpredictor.hpp"

static GATAS::AircraftPositionInfo makePosition(uint32_t timestamp,
                                                GATAS::AircraftAddress address,
                                                float lat,
                                                float lon,
                                                int32_t ellipseHeight,
                                                float verticalSpeed,
                                                float groundSpeed,
                                                int16_t track,
                                                uint32_t distanceFromOwn = 1000)
{
    GATAS::AircraftPositionInfo position;
    position.timestamp = timestamp;
    position.address = address;
    position.lat = lat;
    position.lon = lon;
    position.ellipseHeight = ellipseHeight;
    position.verticalSpeed = verticalSpeed;
    position.groundSpeed = groundSpeed;
    position.track = track;
    position.distanceFromOwn = distanceFromOwn;
    return position;
}

TEST_CASE("AircraftPathPredictor extrapolates straight flight", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    // 5m/s up at 50m/s
    auto latest = makePosition(1'000'000, 0x123456, 52.0f, 4.0f, 1000, 5.0f, 50.0f, 90);
    REQUIRE(predictor.update(latest));
    REQUIRE(predictor.size() == 1);

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, latest);

    auto rel = CoreUtils::northEastDistance(52.0f, 4.0f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(0.0f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(100.0f).margin(1.0f));
    REQUIRE(predicted.timestamp == 3'000'000);
    REQUIRE(predicted.ellipseHeight == 1010); // 1000m + 2s * 5m/s
    REQUIRE(predicted.hTurnRate == Catch::Approx(0.0f));
    REQUIRE(predicted.track == 90);
    REQUIRE(latest.timestamp == 1'000'000);
    REQUIRE(latest.lat == Catch::Approx(52.0f));
    REQUIRE(latest.lon == Catch::Approx(4.0f));
}

TEST_CASE("AircraftPathPredictor uses protocol-provided turn rate from the first sample", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latest = makePosition(1'000'000, 0x123457, 52.0f, 4.0f, 1000, 0.0f, 50.0f, 0);
    latest.hTurnRate = 5.0f;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, latest);

    REQUIRE(predicted.hTurnRate == Catch::Approx(5.0f));
    REQUIRE(predicted.track == 10);
    auto rel = CoreUtils::northEastDistance(52.0f, 4.0f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(99.49f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(8.70f).margin(1.0f));
}

TEST_CASE("AircraftPathPredictor prefers protocol-provided turn rate over derived history", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    REQUIRE(predictor.update(makePosition(9'000'000, 0x123458, 51.99955f, 4.0f, 1000, 0.0f, 50.0f, 350)));
    auto latest = makePosition(10'000'000, 0x123458, 52.0f, 4.0f, 1000, 0.0f, 50.0f, 0);
    latest.hTurnRate = -4.0f;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(12'000'000, latest);

    REQUIRE(predicted.hTurnRate == Catch::Approx(-4.0f));
    REQUIRE(predicted.track == 352);
}

TEST_CASE("AircraftPathPredictor treats zero protocol turn rate as straight flight", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto previous = makePosition(9'000'000, 0x123459, 51.99955f, 4.0f, 1000, 0.0f, 50.0f, 350);
    previous.hTurnRate = NAN;
    REQUIRE(predictor.update(previous));
    auto latest = makePosition(10'000'000, 0x123459, 52.0f, 4.0f, 1000, 0.0f, 50.0f, 0);
    latest.hTurnRate = 0.0f;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(12'000'000, latest);

    REQUIRE(predicted.hTurnRate == Catch::Approx(0.0f));
    REQUIRE(predicted.track == 0);
    auto rel = CoreUtils::northEastDistance(52.0f, 4.0f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(100.0f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(0.0f).margin(1.0f));
}



TEST_CASE("AircraftPathPredictor uses derived positive turn rate for curved prediction", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto previous = makePosition(9'000'000, 0xABCDEF, 51.99955f, 4.0f, 998, 2.0f, 50.0f, 350);
    previous.hTurnRate = NAN;
    REQUIRE(predictor.update(previous));
    auto latest = makePosition(10'000'000, 0xABCDEF, 52.0f, 4.0f, 1000, 2.0f, 50.0f, 0);
    latest.hTurnRate = NAN;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(12'000'000, latest);

    auto rel = CoreUtils::northEastDistance(52.0f, 4.0f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(97.98f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(17.22f).margin(1.0f));
    REQUIRE(predicted.ellipseHeight == 1004);
    REQUIRE(predicted.hTurnRate == Catch::Approx(10.0f));
    REQUIRE(predicted.track == 20);
}

TEST_CASE("AircraftPathPredictor uses derived negative turn rate for right turn prediction", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto previous = makePosition(9'000'000, 0xABCDF0, 51.99955f, 4.0f, 1002, -2.0f, 50.0f, 10);
    previous.hTurnRate = NAN;
    REQUIRE(predictor.update(previous));
    auto latest = makePosition(10'000'000, 0xABCDF0, 52.0f, 4.0f, 1000, -2.0f, 50.0f, 0);
    latest.hTurnRate = NAN;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(12'000'000, latest);

    auto rel = CoreUtils::northEastDistance(52.0f, 4.0f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(97.98f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(-17.22f).margin(1.0f));
    REQUIRE(predicted.ellipseHeight == 996);
    REQUIRE(predicted.hTurnRate == Catch::Approx(-10.0f));
    REQUIRE(predicted.track == 340);
}

TEST_CASE("AircraftPathPredictor preserves int32 altitude range", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latest = makePosition(1'000'000, 0xABCDF1, 52.0f, 4.0f, 40'000, 5.0f, 50.0f, 90);
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, latest);

    REQUIRE(predicted.ellipseHeight == 40'010);
}

TEST_CASE("AircraftPathPredictor derives turn rate from recent history", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto first = makePosition(0, 0x654321, 52.0f, 4.0f, 1000, 0.0f, 50.0f, 350);
    first.hTurnRate = NAN;
    REQUIRE(predictor.update(first));
    auto second = makePosition(1'000'000, 0x654321, 52.00045f, 4.0f, 1000, 0.0f, 50.0f, 0);
    second.hTurnRate = NAN;
    REQUIRE(predictor.update(second));
    auto latest = makePosition(2'000'000, 0x654321, 52.00089f, 4.00008f, 1000, 0.0f, 50.0f, 10);
    latest.hTurnRate = NAN;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, latest);

    REQUIRE(predicted.track == 20);
    auto rel = CoreUtils::northEastDistance(52.00089f, 4.00008f, predicted.lat, predicted.lon);
    REQUIRE(rel.north > 45.0f);
    REQUIRE(rel.east > 10.0f);
}

TEST_CASE("AircraftPathPredictor rejects out-of-order updates and expires tracks", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latest = makePosition(2'000'000, 0x111111, 52.0f, 4.0f, 1000, 0.0f, 40.0f, 180);
    REQUIRE(predictor.update(latest));
    REQUIRE_FALSE(predictor.update(makePosition(1'000'000, 0x111111, 52.0f, 4.0f, 1000, 0.0f, 40.0f, 180)));

    GATAS::AircraftPositionInfo tooEarly = predictor.extrapolatedPos(1'500'000, latest);
    REQUIRE(tooEarly.timestamp == latest.timestamp);
    REQUIRE(tooEarly.lat == Catch::Approx(latest.lat));
    REQUIRE(tooEarly.lon == Catch::Approx(latest.lon));
    REQUIRE(tooEarly.hTurnRate == Catch::Approx(latest.hTurnRate));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(2'500'000, latest);
    REQUIRE(predicted.timestamp == 2'500'000);

    predictor.maintenance(12'000'001);
    REQUIRE(predictor.size() == 0);
    GATAS::AircraftPositionInfo expired = predictor.extrapolatedPos(12'000'001, latest);
    REQUIRE(expired.timestamp == latest.timestamp);
    REQUIRE(expired.lat == Catch::Approx(latest.lat));
    REQUIRE(expired.lon == Catch::Approx(latest.lon));
}

TEST_CASE("AircraftPathPredictor keeps straight flight at low ground speed", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    REQUIRE(predictor.update(makePosition(0, 0x333333, 52.0f, 4.0f, 1000, 0.0f, 50.0f, 0)));
    auto latest = makePosition(1'000'000, 0x333333, 52.00045f, 4.0f, 1000, 0.0f, 1.0f, 0);
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(2'000'000, latest);

    REQUIRE(predicted.track == 0);
    REQUIRE(predicted.hTurnRate == Catch::Approx(0.0f));

    auto rel = CoreUtils::northEastDistance(52.00045f, 4.0f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(1.0f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(0.0f).margin(1.0f));
}

TEST_CASE("AircraftPathPredictor treats exact expiry boundary consistently", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latest = makePosition(1'000'000, 0x444444, 52.0f, 4.0f, 1000, 0.0f, 30.0f, 45);
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(11'000'000, latest);
    REQUIRE(predicted.timestamp == latest.timestamp);
    REQUIRE(predicted.lat == Catch::Approx(latest.lat));
    REQUIRE(predicted.lon == Catch::Approx(latest.lon));

    predictor.maintenance(11'000'000);
    REQUIRE(predictor.size() == 0);
    GATAS::AircraftPositionInfo expired = predictor.extrapolatedPos(11'000'000, latest);
    REQUIRE(expired.timestamp == latest.timestamp);
    REQUIRE(expired.lat == Catch::Approx(latest.lat));
    REQUIRE(expired.lon == Catch::Approx(latest.lon));
}

TEST_CASE("AircraftPathPredictor invalidates stale relative distance on extrapolation", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    GATAS::AircraftPositionInfo input;
    input.timestamp = 2'000'000;
    input.address = 0x555555;
    input.lat = 52.0f;
    input.lon = 4.0f;
    input.ellipseHeight = 1000;
    input.verticalSpeed = 0.0f;
    input.groundSpeed = 25.0f;
    input.track = 180;
    input.distanceFromOwn = 12345;

    REQUIRE(predictor.update(input));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, input);

    REQUIRE(predicted.distanceFromOwn == static_cast<uint32_t>(INT32_MIN));
}

TEST_CASE("AircraftPathPredictor cache hit preserves caller metadata", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latest = makePosition(1'000'000, 0x555556, 52.0f, 4.0f, 1000, 0.0f, 25.0f, 90);
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo first = latest;
    first.callSign = "FIRST";
    first.protocol = GATAS::ConspicuityProtocol::ADSB;
    first.aircraftType = GATAS::AircraftCategory::GLIDER;
    first = predictor.extrapolatedPos(2'000'000, first);

    GATAS::AircraftPositionInfo second = latest;
    second.callSign = "SECOND";
    second.protocol = GATAS::ConspicuityProtocol::OGN;
    second.aircraftType = GATAS::AircraftCategory::LIGHT;
    second = predictor.extrapolatedPos(2'000'000, second);

    REQUIRE(second.callSign == "SECOND");
    REQUIRE(second.protocol == GATAS::ConspicuityProtocol::OGN);
    REQUIRE(second.aircraftType == GATAS::AircraftCategory::LIGHT);
    REQUIRE(first.timestamp == 2'000'000);
    REQUIRE(second.timestamp == 2'000'000);
    REQUIRE(second.lat == Catch::Approx(first.lat));
    REQUIRE(second.lon == Catch::Approx(first.lon));
    REQUIRE(second.hTurnRate == Catch::Approx(first.hTurnRate));
    REQUIRE(second.track == first.track);
    REQUIRE(second.distanceFromOwn == static_cast<uint32_t>(INT32_MIN));
}

TEST_CASE("AircraftPathPredictor rejects update when track map is full", "[single-file]")
{
    AircraftPathPredictor<1> predictor;

    REQUIRE(predictor.update(makePosition(1'000'000, 0x100001, 52.0f, 4.0f, 1000, 0.0f, 20.0f, 0, 1000)));
    REQUIRE_FALSE(predictor.update(makePosition(1'000'000, 0x100002, 52.1f, 4.1f, 1000, 0.0f, 20.0f, 0, 1000)));
    REQUIRE(predictor.size() == 1);
}

TEST_CASE("AircraftPathPredictor reclaims expired slot on full explicit insert", "[single-file]")
{
    AircraftPathPredictor<1> predictor;

    REQUIRE(predictor.update(makePosition(1'000'000, 0x100011, 52.0f, 4.0f, 1000, 0.0f, 20.0f, 0, 1000)));
    REQUIRE(predictor.update(makePosition(12'000'001, 0x100012, 52.1f, 4.1f, 1100, 0.0f, 25.0f, 90, 900)));

    REQUIRE(predictor.size() == 1);
    REQUIRE_FALSE(predictor.contains(0x100011));
    REQUIRE(predictor.contains(0x100012));
}

TEST_CASE("AircraftPathPredictor reclaims expired slot on full position insert", "[single-file]")
{
    AircraftPathPredictor<1> predictor;

    GATAS::AircraftPositionInfo first;
    first.timestamp = 1'000'000;
    first.address = 0x100021;
    first.lat = 52.0f;
    first.lon = 4.0f;
    first.ellipseHeight = 1000;
    first.groundSpeed = 20.0f;
    first.track = 0;
    first.distanceFromOwn = 1000;
    REQUIRE(predictor.update(first));

    GATAS::AircraftPositionInfo second;
    second.timestamp = 12'000'001;
    second.address = 0x100022;
    second.lat = 52.1f;
    second.lon = 4.1f;
    second.ellipseHeight = 1100;
    second.groundSpeed = 25.0f;
    second.track = 90;
    second.distanceFromOwn = 900;
    REQUIRE(predictor.update(second));

    REQUIRE(predictor.size() == 1);
    REQUIRE_FALSE(predictor.contains(0x100021));
    REQUIRE(predictor.contains(0x100022));
}

TEST_CASE("AircraftPathPredictor replaces same-timestamp updates", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    REQUIRE(predictor.update(makePosition(2'000'000, 0x200001, 52.0f, 4.0f, 1000, 0.0f, 10.0f, 0)));
    auto latest = makePosition(2'000'000, 0x200001, 52.001f, 4.001f, 1500, 3.0f, 30.0f, 90);
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, latest);

    auto rel = CoreUtils::northEastDistance(52.001f, 4.001f, predicted.lat, predicted.lon);
    REQUIRE(rel.north == Catch::Approx(0.0f).margin(1.0f));
    REQUIRE(rel.east == Catch::Approx(30.0f).margin(1.0f));
    REQUIRE(predicted.ellipseHeight == 1503);
    REQUIRE(predicted.track == 90);
}

TEST_CASE("AircraftPathPredictor tracks multiple aircraft concurrently", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latestNorthbound = makePosition(1'000'000, 0x300001, 52.0f, 4.0f, 1000, 0.0f, 20.0f, 0);
    auto latestSouthbound = makePosition(1'000'000, 0x300002, 53.0f, 5.0f, 2000, 0.0f, 40.0f, 180);
    REQUIRE(predictor.update(latestNorthbound));
    REQUIRE(predictor.update(latestSouthbound));

    GATAS::AircraftPositionInfo northbound = predictor.extrapolatedPos(2'000'000, latestNorthbound);
    GATAS::AircraftPositionInfo southbound = predictor.extrapolatedPos(2'000'000, latestSouthbound);

    auto relNorthbound = CoreUtils::northEastDistance(52.0f, 4.0f, northbound.lat, northbound.lon);
    auto relSouthbound = CoreUtils::northEastDistance(53.0f, 5.0f, southbound.lat, southbound.lon);
    REQUIRE(relNorthbound.north == Catch::Approx(20.0f).margin(1.0f));
    REQUIRE(relSouthbound.north == Catch::Approx(-40.0f).margin(1.0f));
    REQUIRE(predictor.size() == 2);
}

TEST_CASE("AircraftPathPredictor wraps longitude across the antimeridian", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    auto latest = makePosition(1'000'000, 0x400001, 0.0f, 179.99995f, 1000, 0.0f, 20.0f, 90);
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(2'000'000, latest);

    REQUIRE(predicted.lon < -179.0f);
}

TEST_CASE("AircraftPathPredictor clamps turn rate and normalizes track", "[single-file]")
{
    AircraftPathPredictor<4> predictor;

    REQUIRE(predictor.update(makePosition(1'000'000, 0x500001, 52.0f, 4.0f, 1000, 0.0f, 50.0f, 370)));
    auto latest = makePosition(2'000'000, 0x500001, 52.00045f, 4.0f, 1000, 0.0f, 50.0f, 60);
    latest.hTurnRate = 50.0f;
    REQUIRE(predictor.update(latest));

    GATAS::AircraftPositionInfo predicted = predictor.extrapolatedPos(3'000'000, latest);

    REQUIRE(predicted.track == 75);
}
