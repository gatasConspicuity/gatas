#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>

#include "ace/ownshipstate.hpp"
#include "ace/coreutils.hpp"

using Source = GATAS::PressureSource;
using Origin = GATAS::BarometricSource;

TEST_CASE("Pressure selection prefers origin first then method", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    const auto currentOrigin = GENERATE(Origin::Internal, Origin::External);
    const auto incomingOrigin = GENERATE(Origin::Internal, Origin::External);
    const auto currentMethod = GENERATE(Source::PressureSensor, Source::Calculated, Source::Unavailable);
    const auto incomingMethod = GENERATE(Source::PressureSensor, Source::Calculated, Source::Unavailable);
    state.updateBarometricPressure(currentOrigin, {1013.25f, currentMethod}, 1000);
    const auto snapshot = state.loadPressureState();
    state.updateBarometricPressure(incomingOrigin, {898.7628f, incomingMethod}, 2000);
    const bool incomingPreferred = currentOrigin != incomingOrigin
        ? incomingOrigin == Origin::Internal
        : static_cast<uint8_t>(incomingMethod) >= static_cast<uint8_t>(currentMethod);
    REQUIRE(state.calculatePressureAltitude(2000).value() ==
            Catch::Approx(incomingPreferred ? 1000.0f : 0.0f).margin(0.1f));
    REQUIRE(snapshot.sample(currentOrigin, currentMethod).value.pressurehPa == 1013.25f);
    REQUIRE(state.loadPressureState().sample(incomingOrigin, incomingMethod).value.pressurehPa == 898.7628f);
    if (currentOrigin != incomingOrigin || currentMethod != incomingMethod)
    {
        REQUIRE(state.loadPressureState().sample(currentOrigin, currentMethod).value.pressurehPa == 1013.25f);
    }
}

TEST_CASE("Fresh backup pressure takes over immediately when the preferred sample expires", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    constexpr uint64_t now = 1'800'000'000'000;
    state.updateBarometricPressure(Origin::Internal, {1013.25f, Source::PressureSensor}, now);
    state.updateBarometricPressure(Origin::External, {898.7628f, Source::PressureSensor}, now + 1000);
    state.updateBarometricPressure(Origin::External, {1025.0f, Source::Calculated}, now + 2000);
    REQUIRE(state.calculatePressureAltitude(now + 5000).value() == Catch::Approx(0.0f).margin(0.1f));
    REQUIRE(state.selectedPressureAltitude(now + 5000)->origin == Origin::Internal);
    REQUIRE(state.selectedPressureAltitude(now + 5000)->method == Source::PressureSensor);
    REQUIRE(state.calculatePressureAltitude(now + 5001).value() == Catch::Approx(1000.0f).margin(0.1f));
    REQUIRE(state.selectedPressureAltitude(now + 5001)->origin == Origin::External);
    REQUIRE(state.selectedPressureAltitude(now + 5001)->method == Source::PressureSensor);
    REQUIRE(state.calculatePressureAltitude(now + 6001).value() == Catch::Approx(-97.4f).margin(0.1f));
    REQUIRE(state.selectedPressureAltitude(now + 6001)->origin == Origin::External);
    REQUIRE(state.selectedPressureAltitude(now + 6001)->method == Source::Calculated);
    REQUIRE_FALSE(state.calculatePressureAltitude(now + 7001));
    REQUIRE_FALSE(state.selectedPressureAltitude(now + 7001));
}

TEST_CASE("Invalidating one source immediately selects another fresh source", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    const auto origin = GENERATE(Origin::Internal, Origin::External);
    const auto other = origin == Origin::Internal ? Origin::External : Origin::Internal;
    state.updateBarometricPressure(origin, {1013.25f, Source::PressureSensor}, 1000);
    state.updateBarometricPressure(origin, {898.7628f, Source::Calculated}, 1000);
    state.updateBarometricPressure(other, {GATAS::INVALID_PRESSSURE_HPA, Source::PressureSensor}, 2000);
    REQUIRE(state.calculatePressureAltitude(2000).value() == Catch::Approx(0.0f).margin(0.1f));
    state.updateBarometricPressure(origin, {GATAS::INVALID_PRESSSURE_HPA, Source::PressureSensor}, 2000);
    REQUIRE(state.calculatePressureAltitude(2000).value() == Catch::Approx(1000.0f).margin(0.1f));
    state.updateBarometricPressure(origin, {GATAS::INVALID_PRESSSURE_HPA, Source::Calculated}, 2000);
    REQUIRE_FALSE(state.calculatePressureAltitude(2000));
}

TEST_CASE("Pressure altitude uses ambient pressure without QNH or GPS", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    REQUIRE_FALSE(state.calculatePressureAltitude(100));
    const auto source = GENERATE(Source::PressureSensor, Source::Calculated, Source::Unavailable);
    struct Case { float hpa; float metres; };
    const Case cases[] = {{1013.25f, 0.0f}, {898.7628f, 1000.0f}, {1025.0f, -97.4f}};
    for (const auto &test : cases)
    {
        state.updateBarometricPressure(Origin::Internal, {test.hpa, source}, 100);
        REQUIRE(state.calculatePressureAltitude(100).value() == Catch::Approx(test.metres).margin(0.1f));
    }
}

TEST_CASE("Pressure updates cache altitude together with each sample", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    state.updateBarometricPressure(Origin::External, {898.7628f, Source::PressureSensor}, 100);
    const auto before = state.loadPressureState();
    REQUIRE(before.sample(Origin::External, Source::PressureSensor).pressureAltitudeMetres == Catch::Approx(1000.0f).margin(0.1f));
    REQUIRE(state.calculatePressureAltitude(100).value() == Catch::Approx(1000.0f).margin(0.1f));

    state.updateBarometricPressure(Origin::External, {1025.0f, Source::PressureSensor}, 101);
    const auto after = state.loadPressureState();
    REQUIRE(before.sample(Origin::External, Source::PressureSensor).pressureAltitudeMetres == Catch::Approx(1000.0f).margin(0.1f));
    REQUIRE(after.sample(Origin::External, Source::PressureSensor).pressureAltitudeMetres == Catch::Approx(-97.4f).margin(0.1f));
    REQUIRE(state.calculatePressureAltitude(101).value() == Catch::Approx(-97.4f).margin(0.1f));

    state.updateBarometricPressure(Origin::External, {GATAS::INVALID_PRESSSURE_HPA, Source::PressureSensor}, 102);
    REQUIRE_FALSE(state.loadPressureState().sample(Origin::External, Source::PressureSensor).valid);
    REQUIRE_FALSE(state.calculatePressureAltitude(102));
}

TEST_CASE("Pressure validation includes the specified endpoints and rejects values outside them", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    GATAS::OwnshipPositionInfo location{};
    location.ellipseHeight = 450;
    state.location.store(location);
    for (const float valid : {1.0f, 1100.0f})
    {
        state.updateBarometricPressure(Origin::Internal, {valid, Source::PressureSensor}, 100);
        REQUIRE(state.calculatePressureAltitude(100).has_value());
    }
    for (const float invalid : {GATAS::INVALID_PRESSSURE_HPA, 0.0f, 0.999f, 1100.001f,
                                std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()})
    {
        state.updateBarometricPressure(Origin::Internal, {invalid, Source::PressureSensor}, 100);
        REQUIRE_FALSE(state.calculatePressureAltitude(100));
        REQUIRE(state.location.load().ellipseHeight == 450);
    }
}

TEST_CASE("Out-of-range preferred pressure falls back to a valid source", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    state.updateBarometricPressure(Origin::Internal, {1013.25f, Source::PressureSensor}, 100);
    state.updateBarometricPressure(Origin::External, {898.7628f, Source::PressureSensor}, 100);
    state.updateBarometricPressure(Origin::Internal, {1100.1f, Source::PressureSensor}, 101);
    REQUIRE(state.calculatePressureAltitude(101).value() == Catch::Approx(1000.0f).margin(0.1f));
    state.updateBarometricPressure(Origin::Internal, {1013.25f, Source::PressureSensor}, 102);
    REQUIRE(state.calculatePressureAltitude(102).value() == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("Pressure expiry handles epoch clock corrections", "[ownshipstate]")
{
    GATAS::OwnshipState state;
    state.init(spin_lock_instance(0));
    constexpr uint64_t now = 1'800'000'000'000;
    state.updateBarometricPressure(Origin::Internal, {1013.25f, Source::PressureSensor}, now);
    REQUIRE_FALSE(state.calculatePressureAltitude(now - 1));
    REQUIRE_FALSE(state.calculatePressureAltitude(now + 1'000'000));
    state.updateBarometricPressure(Origin::External, {898.7628f, Source::Calculated}, now - 100'000);
    REQUIRE(state.calculatePressureAltitude(now - 100'000).value() == Catch::Approx(1000.0f).margin(0.1f));
}
