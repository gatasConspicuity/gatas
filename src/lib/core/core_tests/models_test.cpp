#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "models.hpp"

TEST_CASE("ConspicuityProtocol short strings stay compact and stable", "[models]")
{
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::FLARM)) == "fl");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::ADSL_M_BAND)) == "am");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::ADSL_O_BAND_HDR)) == "ah");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::FANET)) == "fa");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::ADSB)) == "ab");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::OGN)) == "og");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::ConspicuityProtocol::NONE)) == "un");
}

TEST_CASE("GATAS Connect output modes select their transports", "[models]")
{
    const GATAS::GatasConnectOutput udp = GATAS::GatasConnectOutput::UDP;
    const GATAS::GatasConnectOutput bluetooth = GATAS::GatasConnectOutput::Bluetooth;
    const GATAS::GatasConnectOutput combined = GATAS::GatasConnectOutput::UDPAndBluetooth;

    REQUIRE(udp.usesUDP());
    REQUIRE_FALSE(udp.usesBluetooth());
    REQUIRE_FALSE(bluetooth.usesUDP());
    REQUIRE(bluetooth.usesBluetooth());
    REQUIRE(combined.usesUDP());
    REQUIRE(combined.usesBluetooth());

    REQUIRE(udp.withBluetooth() == combined);
    REQUIRE(bluetooth.withBluetooth() == bluetooth);
    REQUIRE(combined.withBluetooth() == combined);

    REQUIRE(combined.preferUDP(true) == udp);
    REQUIRE(combined.preferUDP(false) == combined);
    REQUIRE(bluetooth.preferUDP(true) == bluetooth);
    REQUIRE(udp.preferUDP(true) == udp);
}
