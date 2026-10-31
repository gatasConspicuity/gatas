#include <catch2/catch_test_macros.hpp>

#include <etl/array.h>

#include "binarymessages.hpp"
#include "ace/cobs.hpp"
#include "pico/time.h"

TEST_CASE("Aircraft position V1 decodes the Rust unsigned ellipsoid-height range", "[binarymessages]")
{
    constexpr size_t rawSize = 24;
    uint8_t buffer[rawSize] = {};
    etl::bit_stream_writer writer(buffer, rawSize, etl::endian::big);
    writer.write_unchecked(static_cast<uint8_t>(BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V1), 8U);
    writer.write_unchecked(0x010203U, 24U);
    writer.write_unchecked(1U, 8U);
    writer.write_unchecked(5U, 8U);
    writer.write_unchecked(0, 32U);
    writer.write_unchecked(0, 32U);
    writer.write_unchecked(0xFFFFU, 16U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0, 8U);
    writer.write_unchecked(0U, 16U);
    writer.write_unchecked(0, 16U);
    writer.write_unchecked(1U, 8U);
    writer.write_unchecked(0U, 8U);

    etl::bit_stream_reader reader(buffer, rawSize, etl::endian::big);
    const auto position = BinaryMessages::deserializeAircraftPositionV1(0.0f, 0.0f, reader);
    REQUIRE(position.has_value());
    REQUIRE(position->ellipseHeight == 65'435);
    REQUIRE(position->address == 0x010203U);
}

TEST_CASE("Aircraft configuration exposes WiFi mode and dirty state in flags", "[binarymessages]")
{
    etl::array<uint32_t, 2> addresses = {0x010203, 0x040506};
    etl::span<uint32_t> addressSpan(addresses.data(), addresses.size());
    const size_t rawSize = BinaryMessages::serializeAircraftConfigurationSizeV2().items(addresses.size());
    uint8_t buffer[64] = {};

    etl::bit_stream_writer ncWriter(buffer, rawSize, etl::endian::big);
    BinaryMessages::serializeAircraftConfigurationV2(ncWriter, 0x01020304, 0x050607, addressSpan, 0x0a0b0c0d, 0x0e0f10, GATAS::WifiMode::NC, 0xab, false);
    REQUIRE(buffer[0] == BinaryMessages::DataType(BinaryMessages::DataType::AIRCRAFT_CONFIGURATIONS_V2).get_value());
    REQUIRE((buffer[1] & BinaryMessages::AIRCRAFT_CONFIGURATION_WIFI_MODE_MASK) == static_cast<uint8_t>(GATAS::WifiMode::NC));
    const uint8_t expected[] = {
        5, 0, 1, 2, 3, 4, 10, 11, 12, 13, 5, 6, 7,
        GATAS::VERSION_MAJOR, GATAS::VERSION_MINOR, GATAS::VERSION_BUGFIX, 0xab,
        14, 15, 16, 0, 2, 1, 2, 3, 4, 5, 6,
    };
    REQUIRE(rawSize == sizeof(expected));
    for (size_t i = 0; i < sizeof(expected); ++i)
    {
        REQUIRE(buffer[i] == expected[i]);
    }

    etl::bit_stream_writer apWriter(buffer, rawSize, etl::endian::big);
    BinaryMessages::serializeAircraftConfigurationV2(apWriter, 0x01020304, 0x050607, addressSpan, 0x0a0b0c0d, 0x0e0f10, GATAS::WifiMode::AP, 0xab, false);
    REQUIRE((buffer[1] & BinaryMessages::AIRCRAFT_CONFIGURATION_WIFI_MODE_MASK) == static_cast<uint8_t>(GATAS::WifiMode::AP));

    etl::bit_stream_writer clientWriter(buffer, rawSize, etl::endian::big);
    BinaryMessages::serializeAircraftConfigurationV2(clientWriter, 0x01020304, 0x050607, addressSpan, 0x0a0b0c0d, 0x0e0f10, GATAS::WifiMode::CLIENT, 0xab, true);
    REQUIRE((buffer[1] & BinaryMessages::AIRCRAFT_CONFIGURATION_WIFI_MODE_MASK) == static_cast<uint8_t>(GATAS::WifiMode::CLIENT));

    for (const auto mode : {GATAS::WifiMode::NC, GATAS::WifiMode::AP, GATAS::WifiMode::CLIENT})
    {
        for (const bool dirty : {false, true})
        {
            etl::bit_stream_writer writer(buffer, rawSize, etl::endian::big);
            BinaryMessages::serializeAircraftConfigurationV2(writer, 0x01020304, 0x050607, addressSpan, 0x0a0b0c0d, 0x0e0f10, mode, 0xab, dirty);
            REQUIRE(buffer[1] == (static_cast<uint8_t>(mode) | (dirty ? 0x04U : 0U)));
        }
    }
}

TEST_CASE("Framed aircraft configuration includes firmware version without shifting fields", "[binarymessages]")
{
    etl::array<uint32_t, 1> addresses = {0x010203};
    uint8_t framed[64] = {};
    uint8_t raw[64] = {};
    const auto written = BinaryMessages::serializeAircraftConfigurationV2(
        framed, sizeof(framed), 0x01020304, 0x050607,
        etl::span<uint32_t>(addresses.data(), addresses.size()),
        0x0a0b0c0d, 0x0e0f10, GATAS::WifiMode::CLIENT, 0xab, true);
    REQUIRE(written > 0);
    REQUIRE(decodeCOBS(framed, written, raw, sizeof(raw)) == BinaryMessages::serializeAircraftConfigurationSizeV2().items(addresses.size()));
    REQUIRE(raw[1] == (static_cast<uint8_t>(GATAS::WifiMode::CLIENT) | 0x04U));
    REQUIRE(raw[13] == GATAS::VERSION_MAJOR);
    REQUIRE(raw[14] == GATAS::VERSION_MINOR);
    REQUIRE(raw[15] == GATAS::VERSION_BUGFIX);
    REQUIRE(raw[16] == 0xab);
    REQUIRE(raw[17] == 0x0e);
    REQUIRE(raw[18] == 0x0f);
    REQUIRE(raw[19] == 0x10);
    REQUIRE(raw[21] == addresses.size());
    REQUIRE(raw[22] == 1);
    REQUIRE(raw[23] == 2);
    REQUIRE(raw[24] == 3);
}

TEST_CASE("WiFi mode control frame only accepts AP and CLIENT requests", "[binarymessages]")
{
    {
        uint8_t frame[] = {
            BinaryMessages::DataType(BinaryMessages::DataType::SET_WIFI_MODE_V1).get_value(),
            static_cast<uint8_t>(GATAS::WifiMode::CLIENT),
        };
        etl::bit_stream_reader reader(frame, sizeof(frame), etl::endian::big);
        GATAS::WifiMode wifiMode = GATAS::WifiMode::NC;
        REQUIRE(BinaryMessages::deserializeSetWifiModeV1(reader, wifiMode));
        REQUIRE(wifiMode == GATAS::WifiMode::CLIENT);
    }

    {
        uint8_t frame[] = {
            BinaryMessages::DataType(BinaryMessages::DataType::SET_WIFI_MODE_V1).get_value(),
            static_cast<uint8_t>(GATAS::WifiMode::AP),
        };
        etl::bit_stream_reader reader(frame, sizeof(frame), etl::endian::big);
        GATAS::WifiMode wifiMode = GATAS::WifiMode::NC;
        REQUIRE(BinaryMessages::deserializeSetWifiModeV1(reader, wifiMode));
        REQUIRE(wifiMode == GATAS::WifiMode::AP);
    }

    {
        uint8_t frame[] = {
            BinaryMessages::DataType(BinaryMessages::DataType::SET_WIFI_MODE_V1).get_value(),
            static_cast<uint8_t>(GATAS::WifiMode::NC),
        };
        etl::bit_stream_reader reader(frame, sizeof(frame), etl::endian::big);
        GATAS::WifiMode wifiMode = GATAS::WifiMode::AP;
        REQUIRE_FALSE(BinaryMessages::deserializeSetWifiModeV1(reader, wifiMode));
    }
}

TEST_CASE("Aircraft position V2 uses ms-in-minute in the local PPS-aligned frame", "[binarymessages]")
{
    time_us_Value = 20'000'000;
    CoreUtils::setPPS(0);
    CoreUtils::setOffsetMsSinceEpoch(20'000);

    constexpr size_t rawSize = 28;
    uint8_t buffer[rawSize] = {};

    etl::bit_stream_writer writer(buffer, rawSize, etl::endian::big);
    writer.write_unchecked(BinaryMessages::DataType(BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V2).get_value(), 8U);
    writer.write_unchecked(19'000U, 16U);
    writer.write_unchecked(0x010203U, 24U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0, 32U);
    writer.write_unchecked(0, 32U);
    writer.write_unchecked(0xFFFFU, 16U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0, 8U);
    writer.write_unchecked(0U, 16U);
    writer.write_unchecked(0, 16U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(7000, 16U);
    writer.write_unchecked(0U, 8U);

    etl::bit_stream_reader reader(buffer, rawSize, etl::endian::big);
    auto position = BinaryMessages::deserializeAircraftPositionV2(0.0f, 0.0f, reader);

    REQUIRE(position.has_value());
    REQUIRE(position.value().timestamp == 19'000'000);
    REQUIRE(position.value().address == 0x010203U);
    REQUIRE(position.value().ellipseHeight == 65'435);
    REQUIRE(position.value().squawk == 7000);

    buffer[1] = 0x00;
    buffer[2] = 0x00;
    etl::bit_stream_reader staleReader(buffer, rawSize, etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV2(0.0f, 0.0f, staleReader).has_value());
}

TEST_CASE("Aircraft position V3 decodes pressure altitude and skips wire QNH", "[binarymessages]")
{
    time_us_Value = 20'000'000;
    CoreUtils::setPPS(0);
    CoreUtils::setOffsetMsSinceEpoch(20'000);

    constexpr size_t rawSize = 35;
    uint8_t buffer[rawSize] = {};
    etl::bit_stream_writer writer(buffer, rawSize, etl::endian::big);
    writer.write_unchecked(BinaryMessages::DataType(BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V3).get_value(), 8U);
    writer.write_unchecked(19'000U, 16U);
    writer.write_unchecked(0x010203U, 24U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0, 32U);
    writer.write_unchecked(0, 32U);
    writer.write_unchecked(1100U, 16U); // 100 m ellipsoid height, encoded with the V3 +1000 offset.
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(0, 8U);
    writer.write_unchecked(0U, 16U);
    writer.write_unchecked(0, 16U);
    writer.write_unchecked(0U, 8U);
    writer.write_unchecked(7000, 16U);
    writer.write_unchecked(1090, 16U); // 90 m pressure altitude, encoded with +1000.
    writer.write_unchecked(10133, 16U); // 1013.3 hPa, encoded in 0.1 hPa.
    writer.write_unchecked(3U, 8U);
    writer.write_unchecked('A', 8U);
    writer.write_unchecked('B', 8U);
    writer.write_unchecked('C', 8U);

    etl::bit_stream_reader reader(buffer, rawSize, etl::endian::big);
    auto position = BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, reader);

    REQUIRE(position.has_value());
    REQUIRE(position.value().ellipseHeight == 100);
    REQUIRE(position.value().pressureAlt == 90);
    REQUIRE(position->callSign == "ABC");

    buffer[16] = 0;
    buffer[17] = 0;
    etl::bit_stream_reader minimumHeightReader(buffer, rawSize, etl::endian::big);
    const auto minimumHeight = BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, minimumHeightReader);
    REQUIRE(minimumHeight.has_value());
    REQUIRE(minimumHeight->ellipseHeight == -1000);

    buffer[16] = 0xFF;
    buffer[17] = 0xFF;
    etl::bit_stream_reader maximumHeightReader(buffer, rawSize, etl::endian::big);
    const auto maximumHeight = BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, maximumHeightReader);
    REQUIRE(maximumHeight.has_value());
    REQUIRE(maximumHeight->ellipseHeight == 64'535);

    struct PressureCase
    {
        uint16_t raw;
        int32_t altitude;
    };
    const PressureCase cases[] = {
        {0, -1000}, {861, -139}, {1000, 0}, {65534, 64534},
        {65535, GATAS::INVALID_BARO_ALTITUDE},
    };
    for (const auto &test : cases)
    {
        buffer[27] = static_cast<uint8_t>(test.raw >> 8);
        buffer[28] = static_cast<uint8_t>(test.raw);
        etl::bit_stream_reader boundaryReader(buffer, rawSize, etl::endian::big);
        const auto decoded = BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, boundaryReader);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->pressureAlt == test.altitude);
        REQUIRE(decoded->callSign == "ABC");
    }

    buffer[27] = 0x04;
    buffer[28] = 0x42;
    buffer[29] = 0xFF;
    buffer[30] = 0xFF;
    etl::bit_stream_reader missingQnhReader(buffer, rawSize, etl::endian::big);
    const auto missingQnh = BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, missingQnhReader);
    REQUIRE(missingQnh.has_value());
    REQUIRE(missingQnh->pressureAlt == 90);
    REQUIRE(missingQnh->callSign == "ABC");
}

TEST_CASE("Aircraft position decoders reject incomplete and overlong payloads", "[binarymessages]")
{
    uint8_t v1[25] = {};
    v1[0] = BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V1;
    etl::bit_stream_reader shortV1(v1, 23, etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV1(0.0f, 0.0f, shortV1).has_value());
    etl::bit_stream_reader longV1(v1, sizeof(v1), etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV1(0.0f, 0.0f, longV1).has_value());

    uint8_t v2[29] = {};
    v2[0] = BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V2;
    etl::bit_stream_reader shortV2(v2, 27, etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV2(0.0f, 0.0f, shortV2).has_value());
    etl::bit_stream_reader longV2(v2, sizeof(v2), etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV2(0.0f, 0.0f, longV2).has_value());

    uint8_t v3[33] = {};
    v3[0] = BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V3;
    etl::bit_stream_reader shortV3(v3, 31, etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, shortV3).has_value());
    etl::bit_stream_reader longV3(v3, sizeof(v3), etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeAircraftPositionV3(0.0f, 0.0f, longV3).has_value());
}

TEST_CASE("Control message decoders require exact Rust payload sizes", "[binarymessages]")
{
    uint8_t icao[] = {4, 0x01, 0x02, 0x03, 0xFF};
    etl::bit_stream_reader validIcao(icao, 4, etl::endian::big);
    REQUIRE(BinaryMessages::deserializeSetIcaoAddressV1(validIcao) == 0x010203U);
    etl::bit_stream_reader shortIcao(icao, 3, etl::endian::big);
    REQUIRE(BinaryMessages::deserializeSetIcaoAddressV1(shortIcao) == 0U);
    etl::bit_stream_reader longIcao(icao, sizeof(icao), etl::endian::big);
    REQUIRE(BinaryMessages::deserializeSetIcaoAddressV1(longIcao) == 0U);

    uint8_t wifi[] = {7, static_cast<uint8_t>(GATAS::WifiMode::CLIENT), 0xFF};
    GATAS::WifiMode mode = GATAS::WifiMode::NC;
    etl::bit_stream_reader validWifi(wifi, 2, etl::endian::big);
    REQUIRE(BinaryMessages::deserializeSetWifiModeV1(validWifi, mode));
    REQUIRE(mode == GATAS::WifiMode::CLIENT);
    etl::bit_stream_reader longWifi(wifi, sizeof(wifi), etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeSetWifiModeV1(longWifi, mode));
}

TEST_CASE("GDL90 V1 framing matches the Rust type-plus-payload contract", "[binarymessages]")
{
    const uint8_t payload[] = {0x7E, 0x00, 0x01, 0x00, 0x7E};
    uint8_t framed[16] = {};
    const size_t written = BinaryMessages::serializeGdl90V1(
        framed, sizeof(framed), etl::span<const uint8_t>(payload, sizeof(payload)));
    REQUIRE(written > 0);
    REQUIRE(framed[written - 1] == 0U);

    uint8_t raw[16] = {};
    const size_t decoded = decodeCOBS(framed, written, raw, sizeof(raw));
    REQUIRE(decoded == 1 + sizeof(payload));
    REQUIRE(raw[0] == BinaryMessages::DataType::GDL90_V1);
    for (size_t i = 0; i < sizeof(payload); ++i)
    {
        REQUIRE(raw[i + 1] == payload[i]);
    }
}

TEST_CASE("Ownship position V2 requests aircraft response V3", "[binarymessages]")
{
    GATAS::OwnshipPositionInfo ownship{};
    ownship.lat = -1.0f;
    ownship.lon = -2.0f;
    constexpr size_t rawSize = BinaryMessages::serializeOwnshipPositionSizeV2().items(1);
    uint8_t raw[rawSize] = {};
    etl::bit_stream_writer writer(raw, rawSize, etl::endian::big);
    BinaryMessages::serializeOwnshipPositionV2(writer, ownship);

    REQUIRE(raw[0] == BinaryMessages::DataType(BinaryMessages::DataType::AIRCRAFT_POSITION_REQUEST_V2).get_value());
    REQUIRE(raw[rawSize - 1] == 3);
    etl::bit_stream_reader reader(raw, rawSize, etl::endian::big);
    REQUIRE(reader.read_unchecked<uint8_t>(8U) == BinaryMessages::DataType::AIRCRAFT_POSITION_REQUEST_V2);
    reader.skip(4U * 8U + 3U * 8U + 8U + 8U);
    REQUIRE(reader.read_unchecked<int32_t>(32U) == -10'000'000);
    REQUIRE(reader.read_unchecked<int32_t>(32U) == -20'000'000);

    constexpr size_t legacyRawSize = BinaryMessages::serializeOwnshipPositionSizeV1().items(1);
    uint8_t legacyRaw[legacyRawSize] = {};
    etl::bit_stream_writer legacyWriter(legacyRaw, legacyRawSize, etl::endian::big);
    BinaryMessages::serializeOwnshipPositionV1(legacyWriter, ownship);
    REQUIRE(legacyRaw[0] == BinaryMessages::DataType(BinaryMessages::DataType::AIRCRAFT_POSITION_REQUEST_V1).get_value());
}
