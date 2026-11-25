#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "cpr.hpp"

TEST_CASE("CPR NL lookup uses strict latitude boundaries", "[cpr]")
{
    constexpr float firstBoundary = 10.47047130f;
    constexpr float dutchBoundary = 53.09516153f;
    constexpr float finalBoundary = 87.0f;

    REQUIRE(59 == cprNLFunction(0.0f));
    REQUIRE(59 == cprNLFunction(10.4f));
    REQUIRE(59 == cprNLFunction(std::nextafter(firstBoundary, 0.0f)));
    REQUIRE(58 == cprNLFunction(firstBoundary));
    REQUIRE(36 == cprNLFunction(52.0f));
    REQUIRE(36 == cprNLFunction(std::nextafter(dutchBoundary, 0.0f)));
    REQUIRE(35 == cprNLFunction(dutchBoundary));
    REQUIRE(2 == cprNLFunction(86.9f));
    REQUIRE(1 == cprNLFunction(finalBoundary));
}

TEST_CASE("CPR NL lookup is symmetric around the equator", "[cpr]")
{
    constexpr float latitude = 52.0f;

    REQUIRE(cprNLFunction(latitude) == cprNLFunction(-latitude));
}

TEST_CASE("CPR NL lookup clamps outlier latitudes to one zone", "[cpr]")
{
    REQUIRE(1 == cprNLFunction(87.1f));
    REQUIRE(1 == cprNLFunction(90.0f));
    REQUIRE(1 == cprNLFunction(-90.0f));
    REQUIRE(1 == cprNLFunction(120.0f));
    REQUIRE(1 == cprNLFunction(-120.0f));
}
