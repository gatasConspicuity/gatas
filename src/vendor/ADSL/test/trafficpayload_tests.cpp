
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../include/adsl/errorCorrect.hpp"
#include "../include/adsl/trafficpayload.hpp"

#include "etl/vector.h"
#include "helpers.hpp"

using namespace ADSL;

// ============================================================================
// Default Constructor Tests
// ============================================================================

TEST_CASE("TrafficPayload Default Constructor", "[TrafficPayload]")
{
  TrafficPayload payload;

  REQUIRE(TrafficPayload::FlightState::UNDEFINED == 0);
  REQUIRE(TrafficPayload::FlightState::ON_GROUND == 1);
  REQUIRE(TrafficPayload::FlightState::AIRBORNE == 2);
  REQUIRE(payload.flightState() == TrafficPayload::FlightState::ON_GROUND);
  REQUIRE(payload.aircraftCategory() == TrafficPayload::AircraftCategory::NO_INFO);
  REQUIRE(payload.emergencyStatus() == TrafficPayload::EmergencyStatus::NO_EMERGENCY);
  REQUIRE(payload.sourceIntegrityLevel() == TrafficPayload::SourceIntegrityLevel::UNDEFINED_SIL);
  REQUIRE(payload.designAssurance() == TrafficPayload::DesignAssurance::UNDEFINED_NONE);
  REQUIRE(payload.navigationIntegrity() == TrafficPayload::NavigationIntegrity::UNDEFINED);
  REQUIRE(payload.horizontalPositionAccuracy() == TrafficPayload::HorizontalPositionAccuracy::UNKNOWN_NO_FIX);
  REQUIRE(payload.verticalPositionAccuracy() == TrafficPayload::VerticalPositionAccuracy::UNKNOWN_NO_FIX);
  REQUIRE(payload.velocityAccuracy() == TrafficPayload::VelocityAccuracy::UNKNOWN_NO_FIX);
  REQUIRE(payload.timestampQms() == 0);

  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { payload.serialize_issue1(writer); });
  auto reader = createReader(packet);
  REQUIRE(reader.read_unchecked<uint8_t>(6U) == 0U);
  REQUIRE(reader.read_unchecked<uint8_t>(2U) == 1U);
  REQUIRE(reader.read_unchecked<uint8_t>(5U) == 0U);
  REQUIRE(reader.read_unchecked<uint8_t>(3U) == 1U);
  REQUIRE(reader.read_unchecked<uint32_t>(24U) == 0x800000U);
  REQUIRE(reader.read_unchecked<uint32_t>(24U) == 0x800000U);
  REQUIRE(reader.read_unchecked<uint8_t>(8U) == 0xFFU);
  REQUIRE(reader.read_unchecked<uint16_t>(14U) == 0x3FFFU);
  REQUIRE(reader.read_unchecked<uint16_t>(9U) == 0x1FFU);
}

TEST_CASE("TrafficPayload Serialize/Deserialize Roundtrip Values", "[TrafficPayload]")
{
  TrafficPayload layer;
  layer.latitude(50.0f);
  layer.longitude(4.0f);
  layer.speed(12.5f);
  layer.altitude(12876);
  layer.verticalRate(13.2f);
  layer.groundTrack(123.4f);
  layer.flightState(TrafficPayload::FlightState::UNDEFINED);
  layer.aircraftCategory(TrafficPayload::AircraftCategory::UAS_OPEN_CATEGORY);
  layer.emergencyStatus(TrafficPayload::EmergencyStatus::UNLAWFUL_INTERFERENCE);
  layer.sourceIntegrityLevel(TrafficPayload::SourceIntegrityLevel::SIL_1E5);
  layer.designAssurance(TrafficPayload::DesignAssurance::DAL_C);
  layer.navigationIntegrity(TrafficPayload::NavigationIntegrity::RC_25_75M);
  layer.horizontalPositionAccuracy(TrafficPayload::HorizontalPositionAccuracy::HFOM_3_10M);
  layer.verticalPositionAccuracy(TrafficPayload::VerticalPositionAccuracy::VFOM_10_45M);
  layer.velocityAccuracy(TrafficPayload::VelocityAccuracy::ACC_VEL_1_3MS);

  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { 
    layer.serialize_issue1(writer); 
     REQUIRE( 120 == writer.size_bits()); });
  auto reader = createReader(packet);
  auto received = TrafficPayload::deserialize_issue1(reader);

  // Check numeric fields with reasonable tolerances
  REQUIRE(received.timestampQms() == layer.timestampQms());
  REQUIRE(received.flightState() == layer.flightState());
  REQUIRE(received.aircraftCategory() == layer.aircraftCategory());
  REQUIRE(received.emergencyStatus() == layer.emergencyStatus());

  REQUIRE(received.latitude() == Catch::Approx(layer.latitude()).margin(1e-3f));
  REQUIRE(received.longitude() == Catch::Approx(layer.longitude()).margin(1e-3f));
  REQUIRE(received.speed() == Catch::Approx(layer.speed()).margin(1e-2f));
  REQUIRE(received.altitude() == layer.altitude());
  REQUIRE(received.verticalRate() == Catch::Approx(layer.verticalRate()).margin(1e-2f));
  REQUIRE(received.groundTrack() == Catch::Approx(layer.groundTrack()).margin(1e-2f));

  REQUIRE(received.sourceIntegrityLevel() == layer.sourceIntegrityLevel());
  REQUIRE(received.designAssurance() == layer.designAssurance());
  REQUIRE(received.navigationIntegrity() == layer.navigationIntegrity());
  REQUIRE(received.horizontalPositionAccuracy() == layer.horizontalPositionAccuracy());
  REQUIRE(received.verticalPositionAccuracy() == layer.verticalPositionAccuracy());
  REQUIRE(received.velocityAccuracy() == layer.velocityAccuracy());
}

TEST_CASE("TrafficPayload signed coordinates survive wire round-trip", "[TrafficPayload]")
{
  // ADS-L Issue 2, G.1.5: signed 24-bit coordinates, with 1 degree encoded
  // as 93206 latitude units and 46603 longitude units.
  TrafficPayload payload;
  payload.latitude(-1.0f);
  payload.longitude(-1.0f);

  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { payload.serialize_issue1(writer); });
  auto reader = createReader(packet);
  reader.skip(16U);
  REQUIRE(reader.read_unchecked<uint32_t>(24U) == 0xFE93EAU);
  REQUIRE(reader.read_unchecked<uint32_t>(24U) == 0xFF49F5U);
  auto decodeReader = createReader(packet);
  const auto decoded = TrafficPayload::deserialize_issue1(decodeReader);

  REQUIRE(decoded.latitude() == Catch::Approx(-1.0f).margin(1e-5f));
  REQUIRE(decoded.longitude() == Catch::Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("TrafficPayload Latitude ", "[single-file]")
{
  TrafficPayload payload;
  payload.latitude(56.95812f);
  REQUIRE(payload.latitude() == Catch::Approx(56.95812f).margin(0.000001));
  payload.latitude(-56.18748);
  REQUIRE(payload.latitude() == Catch::Approx(-56.18748f).margin(0.000001));
  payload.latitude(-89.999);
  REQUIRE(payload.latitude() == Catch::Approx(-89.999).margin(0.000001));
  payload.latitude(89.999);
  REQUIRE(payload.latitude() == Catch::Approx(89.999).margin(0.000001));
}

TEST_CASE("TrafficPayload Longitude ", "[single-file]")
{
  TrafficPayload payload;
  payload.longitude(160.54197);
  REQUIRE(payload.longitude() == Catch::Approx(160.54197).margin(0.00002));
  payload.longitude(-126.74510);
  REQUIRE(payload.longitude() == Catch::Approx(-126.74510).margin(0.00002));
  payload.longitude(-179.999);
  REQUIRE(payload.longitude() == Catch::Approx(-180).margin(0.00002));
  payload.longitude(179.999);
  REQUIRE(payload.longitude() == Catch::Approx(180).margin(0.00002));
}

TEST_CASE("TrafficPayload altitude ", "[single-file]")
{
  TrafficPayload payload;

  payload.altitude(-400);
  REQUIRE(payload.altitude() == -320);

  payload.altitude(62000);
  // ADS-L Issue 2, G.1.7: saturation uses the maximum valid code 0x3FFE.
  REQUIRE(payload.altitude() == 61104);
  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { payload.serialize_issue1(writer); });
  auto reader = createReader(packet);
  reader.skip(72U);
  REQUIRE(reader.read_unchecked<uint16_t>(14U) == 0x3FFEU);

  payload.altitude(0);
  REQUIRE(payload.altitude() == 0);

  payload.altitude(1000);
  packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                             { payload.serialize_issue1(writer); });
  auto exampleReader = createReader(packet);
  exampleReader.skip(72U);
  REQUIRE(exampleReader.read_unchecked<uint16_t>(14U) == 0x0528U);
  REQUIRE(payload.altitude() == 1000);

  payload.altitude(12000);
  REQUIRE(payload.altitude() == 12000);
}

TEST_CASE("TrafficPayload speed ", "[single-file]")
{
  TrafficPayload payload;
  payload.speed(0);
  REQUIRE(payload.speed() == Catch::Approx(0).margin(0.5));

  payload.speed(20.0);
  REQUIRE(payload.speed() == Catch::Approx(20).margin(0.5));

  payload.speed(100.0);
  REQUIRE(payload.speed() == Catch::Approx(100.0).margin(2.5));

  payload.speed(120.0f);
  // ADS-L Issue 2, G.1.8 publishes 120 m/s -> 0xC4.
  REQUIRE(payload.speed() == Catch::Approx(120.0f).margin(0.01f));
  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { payload.serialize_issue1(writer); });
  auto reader = createReader(packet);
  reader.skip(64U);
  REQUIRE(reader.read_unchecked<uint8_t>(8U) == 0xC4U);

  payload.speed(237);
  REQUIRE(payload.speed() == Catch::Approx(236).margin(2.5));
}

TEST_CASE("TrafficPayload verticalrate ", "[single-file]")
{
  TrafficPayload payload;
  payload.verticalRate(-125.0f);
  REQUIRE(payload.verticalRate() == Catch::Approx(-118.0f).margin(0.125 / 2));

  payload.verticalRate(125.0f);
  REQUIRE(payload.verticalRate() == Catch::Approx(119.0).margin(0.125 / 2));

  payload.verticalRate(-10.125f);
  REQUIRE(payload.verticalRate() == Catch::Approx(-10.0f).margin(0.01f));
  payload.verticalRate(10.0f);
  // ADS-L Issue 2, G.1.9 publishes 10 m/s -> 0x048 and -0.125 m/s -> 0x101.
  REQUIRE(payload.verticalRate() == Catch::Approx(10.0f).margin(0.01f));
  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { payload.serialize_issue1(writer); });
  auto reader = createReader(packet);
  reader.skip(86U);
  REQUIRE(reader.read_unchecked<uint16_t>(9U) == 0x048U);

  payload.verticalRate(-0.125f);
  packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                             { payload.serialize_issue1(writer); });
  auto negativeReader = createReader(packet);
  negativeReader.skip(86U);
  REQUIRE(negativeReader.read_unchecked<uint16_t>(9U) == 0x101U);
  payload.verticalRate(5.8f);
  REQUIRE(payload.verticalRate() == Catch::Approx(5.75f).margin(0.125 / 2));
}

TEST_CASE("TrafficPayload groundTrack ", "[single-file]")
{
  TrafficPayload payload;
  payload.groundTrack(0.0f);
  REQUIRE(payload.groundTrack() == Catch::Approx(0.0f).margin(0.7));

  payload.groundTrack(360.0f);
  REQUIRE(payload.groundTrack() == Catch::Approx(0.0).margin(0.7));

  payload.groundTrack(78.0f);
  REQUIRE(payload.groundTrack() == Catch::Approx(78.0).margin(0.7));

  payload.groundTrack(-5.0f);
  REQUIRE(payload.groundTrack() == Catch::Approx(355.0).margin(0.7));

  payload.groundTrack(370.0f);
  REQUIRE(payload.groundTrack() == Catch::Approx(10.0).margin(0.7));
}

TEST_CASE("TrafficPayload integrity ", "[single-file]")
{
  TrafficPayload payload;
  payload.sourceIntegrityLevel(TrafficPayload::SourceIntegrityLevel::SIL_1E5);
  REQUIRE(payload.sourceIntegrityLevel() == TrafficPayload::SourceIntegrityLevel::SIL_1E5);

  payload.designAssurance(TrafficPayload::DesignAssurance::DAL_C);
  REQUIRE(payload.designAssurance() == TrafficPayload::DesignAssurance::DAL_C);

  payload.navigationIntegrity(TrafficPayload::NavigationIntegrity::RC_75M_0_1NM);
  REQUIRE(payload.navigationIntegrity() == TrafficPayload::NavigationIntegrity::RC_75M_0_1NM);

  payload.horizontalPositionAccuracy(TrafficPayload::HorizontalPositionAccuracy::HFOM_30M_0_05NM);
  REQUIRE(payload.horizontalPositionAccuracy() == TrafficPayload::HorizontalPositionAccuracy::HFOM_30M_0_05NM);

  payload.verticalPositionAccuracy(TrafficPayload::VerticalPositionAccuracy::VFOM_10_45M);
  REQUIRE(payload.verticalPositionAccuracy() == TrafficPayload::VerticalPositionAccuracy::VFOM_10_45M);

  payload.velocityAccuracy(TrafficPayload::VelocityAccuracy::ACC_VEL_1_3MS);
  REQUIRE(payload.velocityAccuracy() == TrafficPayload::VelocityAccuracy::ACC_VEL_1_3MS);

  payload.aircraftCategory(TrafficPayload::AircraftCategory::MODEL_PLANE);
  REQUIRE(payload.aircraftCategory() == TrafficPayload::AircraftCategory::MODEL_PLANE);

  payload.emergencyStatus(TrafficPayload::EmergencyStatus::LIFEGUARD_MEDICAL);
  REQUIRE(payload.emergencyStatus() == TrafficPayload::EmergencyStatus::LIFEGUARD_MEDICAL);

  payload.flightState(TrafficPayload::FlightState::AIRBORNE);
  REQUIRE(payload.flightState() == TrafficPayload::FlightState::AIRBORNE);
}

TEST_CASE("TrafficPayload serialize", "[single-file]")
{
  TrafficPayload payload;
  auto packet = createRadioPacket([&](etl::bit_stream_writer &writer)
                                  { payload.serialize_issue1(writer); });

  // printBufferHex(etl::span(packet.data(), packet.size()));
  REQUIRE(packet == makeVector({0x02, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00}));
}

TEST_CASE("Timestamp Restore keeps received timestamps in the past", "single-file")
{
  TrafficPayload payload;
  auto receiveTimestamp = 1771009961749ULL;
  auto sendTimestamp = receiveTimestamp - 5600ULL;
  payload.timestamp(sendTimestamp);

  REQUIRE(1771009956000LL == payload.timestampRestored(receiveTimestamp));
}

TEST_CASE("Timestamp Restore rolls numerically future ticks to previous cycle", "single-file")
{
  TrafficPayload payload;
  auto receiveTimestamp = 1771009965500ULL;
  auto sendTimestamp = receiveTimestamp - 1000ULL;
  payload.timestamp(sendTimestamp);

  REQUIRE(1771009964500LL == payload.timestampRestored(receiveTimestamp));
}

TEST_CASE("Timestamp Restore rolls a candidate 2000ms in the future to the previous cycle", "single-file")
{
  TrafficPayload payload;
  auto receiveTimestamp = 1771009962500ULL;
  auto sendTimestamp = 1771009949623ULL;
  payload.timestamp(sendTimestamp);

  REQUIRE(1771009949500LL == payload.timestampRestored(receiveTimestamp));
}
  
