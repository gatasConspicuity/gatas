
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../include/adsl/errorCorrect.hpp"
#include "../include/adsl/statuspayload.hpp"
#include "etl/vector.h"
#include "helpers.hpp"

using namespace ADSL;

// ============================================================================
// Default Constructor Tests
// ============================================================================

TEST_CASE("StatusPayload Default Constructor", "[StatusPayload]")
{
    StatusPayload payload;

    REQUIRE(payload.type() == Header::PayloadTypeIdentifier::STATUS);
    REQUIRE(payload.mobileState() == StatusPayload::MobileState::None);
    REQUIRE(payload.xpdrCapability() == StatusPayload::XpdrCapability::NoneOrOff);
    REQUIRE(payload.eReceiveConspicuityBits() ==
            StatusPayload::EConspicuityBits::EC_NONE);
    REQUIRE(payload.eTransmitConspicuityBits() ==
            StatusPayload::EConspicuityBits::EC_NONE);
    REQUIRE(payload.adslTrafficUplinkClient() == false);
    REQUIRE(payload.adslFisBUplinkCLient() == false);
    REQUIRE(payload.maxProtocolversion() ==
            NetworkPayload::ProtocolVersion::ISSUE_2);

    auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                    { payload.serialize_issue2(writer); });

    REQUIRE(packet == makeVector({0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
}

// ============================================================================
// MobileState Tests
// ============================================================================

TEST_CASE("StatusPayload MobileState Getter/Setter", "[StatusPayload]")
{
    StatusPayload payload;

    payload.mobileState(StatusPayload::MobileState::None);
    REQUIRE(payload.mobileState() == StatusPayload::MobileState::None);

    payload.mobileState(StatusPayload::MobileState::Offline);
    REQUIRE(payload.mobileState() == StatusPayload::MobileState::Offline);
}

// ============================================================================
// XpdrCapability Tests
// ============================================================================

TEST_CASE("StatusPayload XpdrCapability Getter/Setter", "[StatusPayload]")
{
    StatusPayload payload;

    payload.xpdrCapability(StatusPayload::XpdrCapability::NoneOrOff);
    REQUIRE(payload.xpdrCapability() == StatusPayload::XpdrCapability::NoneOrOff);

    payload.xpdrCapability(StatusPayload::XpdrCapability::ModeC);
    REQUIRE(payload.xpdrCapability() == StatusPayload::XpdrCapability::ModeC);
}

TEST_CASE("StatusPayload keeps each band capability independent", "[StatusPayload]")
{
    StatusPayload payload;
    payload.mBandReceiveCapability(StatusPayload::ReceiveCapability::Occasional);
    payload.oBandReceiveCapability(StatusPayload::ReceiveCapability::Partial);
    payload.oBandHdrReceiveCapability(StatusPayload::ReceiveCapability::Full);

    REQUIRE(payload.mBandReceiveCapability() == StatusPayload::ReceiveCapability::Occasional);
    REQUIRE(payload.oBandReceiveCapability() == StatusPayload::ReceiveCapability::Partial);
    REQUIRE(payload.oBandHdrReceiveCapability() == StatusPayload::ReceiveCapability::Full);

    auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                    { payload.serialize_issue2(writer); });
    auto reader = createReader(packet);
    const auto decoded = StatusPayload::deserialize_issue2(reader);
    REQUIRE(decoded.mBandReceiveCapability() == StatusPayload::ReceiveCapability::Occasional);
    REQUIRE(decoded.oBandReceiveCapability() == StatusPayload::ReceiveCapability::Partial);
    REQUIRE(decoded.oBandHdrReceiveCapability() == StatusPayload::ReceiveCapability::Full);
}

// ============================================================================
// EConspicuityBits Tests - Receive
// ============================================================================

TEST_CASE("StatusPayload eReceiveConspicuityBits Single Bit",
          "[StatusPayload]")
{
    StatusPayload payload;

    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_FLARM);
    REQUIRE(payload.eReceiveConspicuityBits() ==
            StatusPayload::EConspicuityBits::EC_FLARM);
}

TEST_CASE("StatusPayload eReceiveConspicuityBits All Bits", "[StatusPayload]")
{
    StatusPayload payload;

    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_FLARM);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_PILOTAWARE);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_FANET);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_OGN);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_ADSB_1090);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_ADSB_978);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_MODES_PCAS);

    uint8_t expected = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6);
    REQUIRE(static_cast<uint8_t>(payload.eReceiveConspicuityBits()) == expected);
}

// ============================================================================
// EConspicuityBits Tests - Transmit
// ============================================================================

TEST_CASE("StatusPayload eTransmitConspicuityBits Single Bit",
          "[StatusPayload]")
{
    StatusPayload payload;

    payload.eTransmitConspicuityBits(StatusPayload::EConspicuityBits::EC_OGN);
    REQUIRE(payload.eTransmitConspicuityBits() ==
            StatusPayload::EConspicuityBits::EC_OGN);
}

TEST_CASE("StatusPayload eTransmitConspicuityBits Multiple Bits OR",
          "[StatusPayload]")
{
    StatusPayload payload;

    // Set ADSB_1090 bit
    payload.eTransmitConspicuityBits(
        StatusPayload::EConspicuityBits::EC_ADSB_1090);
    REQUIRE(static_cast<uint8_t>(payload.eTransmitConspicuityBits()) ==
            static_cast<uint8_t>(StatusPayload::EConspicuityBits::EC_ADSB_1090));

    // OR with ADSB_978 bit
    payload.eTransmitConspicuityBits(
        StatusPayload::EConspicuityBits::EC_ADSB_978);
    uint8_t expected =
        static_cast<uint8_t>(StatusPayload::EConspicuityBits::EC_ADSB_1090) |
        static_cast<uint8_t>(StatusPayload::EConspicuityBits::EC_ADSB_978);
    REQUIRE(static_cast<uint8_t>(payload.eTransmitConspicuityBits()) == expected);
}

// ============================================================================
// ADSL Uplink Client Tests
// ============================================================================

TEST_CASE("StatusPayload adslTrafficUplinkClient Getter/Setter",
          "[StatusPayload]")
{
    StatusPayload payload;

    payload.adslTrafficUplinkClient(true);
    REQUIRE(payload.adslTrafficUplinkClient() == true);

    payload.adslTrafficUplinkClient(false);
    REQUIRE(payload.adslTrafficUplinkClient() == false);
}

TEST_CASE("StatusPayload adslFisBUplinkCLient Getter/Setter",
          "[StatusPayload]")
{
    StatusPayload payload;

    payload.adslFisBUplinkCLient(true);
    REQUIRE(payload.adslFisBUplinkCLient() == true);

    payload.adslFisBUplinkCLient(false);
    REQUIRE(payload.adslFisBUplinkCLient() == false);
}

// ============================================================================
// Serialization/Deserialization Tests
// ============================================================================

TEST_CASE("StatusPayload serialize/deserialize complete", "[StatusPayload]")
{
    StatusPayload payload;
    payload.mobileState(StatusPayload::MobileState::Online);
    payload.xpdrCapability(StatusPayload::XpdrCapability::ModeS);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_FLARM);
    payload.eReceiveConspicuityBits(StatusPayload::EConspicuityBits::EC_ADSB_1090);
    payload.eTransmitConspicuityBits(StatusPayload::EConspicuityBits::EC_ADSB_1090);
    payload.adslTrafficUplinkClient(true);
    payload.adslFisBUplinkCLient(false);

    auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                    { payload.serialize_issue2(writer);                                  
                                      REQUIRE( 56 == writer.size_bits() ); });

    REQUIRE(packet == makeVector({0x80, 0x18, 0x80, 0x86, 0x00, 0x00, 0x00}));

    auto reader = createReader(packet);
    auto received = StatusPayload::deserialize_issue2(reader);

    REQUIRE(received.mobileState() == StatusPayload::MobileState::Online);
    REQUIRE(received.xpdrCapability() == StatusPayload::XpdrCapability::ModeS);

    uint8_t expectedReceive = static_cast<uint8_t>(StatusPayload::EConspicuityBits::EC_FLARM) | static_cast<uint8_t>(StatusPayload::EConspicuityBits::EC_ADSB_1090);
    REQUIRE(static_cast<uint8_t>(received.eReceiveConspicuityBits()) == expectedReceive);

    uint8_t expectedTransmit = static_cast<uint8_t>(StatusPayload::EConspicuityBits::EC_ADSB_1090);
    REQUIRE(static_cast<uint8_t>(received.eTransmitConspicuityBits()) == expectedTransmit);

    REQUIRE(received.adslTrafficUplinkClient() == true);
    REQUIRE(received.adslFisBUplinkCLient() == false);
    REQUIRE(received.maxProtocolversion() == NetworkPayload::ProtocolVersion::LATEST);
}
