
#pragma once

#include <cmath>

#include <etl/bit_stream.h>
#include <etl/algorithm.h>
#include <etl/span.h>
#include <etl/absolute.h>
#include <etl/optional.h>
#include <etl/to_arithmetic.h>
#include "ace/cobs.hpp"
#include "lib_crc.hpp"
#include "coreutils.hpp"
#include "models.hpp"
#include "ace/measure.hpp"

class BinaryMessages
{
public:
    // Upper bound for one framed COBS payload including the trailing zero delimiter.
    static constexpr size_t MAX_COBS_FRAME_SIZE = 256;
    static constexpr size_t DEVICE_ACTION_REQUEST_SIZE_V1 = 3;

    /**
     * Object that can be used by binary messages to indicate how many bytes it will consume
     * A function 'items' is provided to calculate the number of items that can be expected to calculate
     * total length.
     */
    struct SizeType
    {
        size_t base;
        size_t size;
        constexpr size_t items(size_t items = 1)
        {
            return base + size * items;
        };
    };

    // Assigments follows closly GDL90 specification
    struct DataType
    {
        enum enum_type : uint8_t
        {
            AIRCRAFT_POSITION_TYPE_V1 = 1,     // Deprecated BinaryMessage type of an aircraft other than our own, this can be injexted in the system to process and display
            AIRCRAFT_POSITION_REQUEST_V1 = 2,  // Binary message of a request for other aircraft from gatasConnect
                                               //          AIRCRAFT_CONFIGURATIONS_V1 = 3,   // Deprecated, see AIRCRAFT_CONFIGURATIONS_V2 Current GATAS COnfiguration 1.0.0-prerelease
            SET_ICAO_ADDRESS_V1 = 4,           // Set a new aircraft configuration based on hexcode, this is like if you set from teh AI a other aircraft
            AIRCRAFT_CONFIGURATIONS_V2 = 5,    // Current GATAS COnfiguration V2
            GDL90_V1 = 6,                      // Packed GDL90 message for bridge transports
            SET_WIFI_MODE_V1 = 7,              // Request that OpenAce changes WiFi mode
            AIRCRAFT_POSITION_TYPE_V2 = 8,     // BinaryMessage type of an aircraft other than our own, this can be injexted in the system to process and display
            AIRCRAFT_POSITION_TYPE_V3 = 9,     // Aircraft position with pressure altitude and QNH
            AIRCRAFT_POSITION_REQUEST_V2 = 10, // Request aircraft positions using the versioned response format
            OWNSHIP_PRESSURE_V1 = 11, // Ownship ambient pressure and QNH
            DEVICE_ACTION_REQUEST_V1 = 12,     // Action and current 8-bit boot ID
        };

        ETL_DECLARE_ENUM_TYPE(DataType, uint8_t)
        ETL_ENUM_TYPE(AIRCRAFT_POSITION_TYPE_V1, "Aircraft Data")
        ETL_ENUM_TYPE(AIRCRAFT_POSITION_REQUEST_V1, "conspicuity Data Request")
        //      ETL_ENUM_TYPE(AIRCRAFT_CONFIGURATIONS_V1, "Current GATAS Configuration see AIRCRAFT_CONFIGURATIONS_V2")
        ETL_ENUM_TYPE(SET_ICAO_ADDRESS_V1, "Set new aircraft from configuration")
        ETL_ENUM_TYPE(AIRCRAFT_CONFIGURATIONS_V2, "Current GATAS Configuration")
        ETL_ENUM_TYPE(GDL90_V1, "GDL90 Message")
        ETL_ENUM_TYPE(SET_WIFI_MODE_V1, "Set WiFi Mode")
        ETL_ENUM_TYPE(AIRCRAFT_POSITION_TYPE_V2, "Aircraft Data")
        ETL_ENUM_TYPE(AIRCRAFT_POSITION_TYPE_V3, "Aircraft Data with pressure altitude and QNH")
        ETL_ENUM_TYPE(AIRCRAFT_POSITION_REQUEST_V2, "Versioned conspicuity data request")
        ETL_ENUM_TYPE(OWNSHIP_PRESSURE_V1, "Ownship ambient pressure and QNH")
        ETL_ENUM_TYPE(DEVICE_ACTION_REQUEST_V1, "Device action request")
        ETL_END_ENUM_TYPE
    };

    static etl::optional<GATAS::DeviceActionRequest> deserializeDeviceActionRequestV1(etl::span<const uint8_t> data)
    {

        if (data.size() != DEVICE_ACTION_REQUEST_SIZE_V1 || data[0] != DataType::DEVICE_ACTION_REQUEST_V1 ||
            data[1] < static_cast<uint8_t>(GATAS::DeviceAction::Save) ||
            data[1] > static_cast<uint8_t>(GATAS::DeviceAction::UsbBoot))
        {
            return etl::nullopt;
        }
        return GATAS::DeviceActionRequest{static_cast<GATAS::DeviceAction>(data[1]), data[2]};
    }

    static constexpr uint8_t AIRCRAFT_CONFIGURATION_WIFI_MODE_MASK = 0x03U;
    static constexpr uint8_t AIRCRAFT_CONFIGURATION_DIRTY_FLAG = 0x04U;

    static constexpr int32_t LEGACY_ELLIPSOID_HEIGHT_OFFSET_M = 100;
    static constexpr int32_t V3_ELLIPSOID_HEIGHT_OFFSET_M = 1000;
    static constexpr int32_t PRESSURE_ALTITUDE_OFFSET_M = 1000;

    static bool hasValidAircraftPositionSize(const etl::bit_stream_reader &reader, size_t fixedSize)
    {
        const auto data = reader.data();
        if (data.size() < fixedSize)
        {
            return false;
        }
        const uint8_t callSignLength = static_cast<uint8_t>(data[fixedSize - 1]);
        return callSignLength <= GATAS::MAX_CALLSIGN_LENGTH && data.size() == fixedSize + callSignLength;
    }

    // Six-byte payload: type (u8), pressure (u16, 0.1 hPa),
    // QNH (u16, 0.1 hPa), source (u8). Multi-byte fields are big endian.
    // Pressure 0xffff is unavailable. QNH is consumed but not stored.
    static etl::optional<GATAS::BarometricPressure> deserializeOwnshipPressureV1(etl::bit_stream_reader &reader)
    {
        if (reader.size_bytes() != 6U)
        {
            return etl::nullopt;
        }
        const auto type = reader.read<uint8_t>();
        if (!type || type.value() != DataType::OWNSHIP_PRESSURE_V1)
        {
            return etl::nullopt;
        }
        const auto pressure = reader.read<uint16_t>();
        const auto qnh = reader.read<uint16_t>();
        const auto source = reader.read<uint8_t>();
        if (!pressure || !qnh || !source ||
            source.value() >= static_cast<uint8_t>(GATAS::PressureSource::PRESSURE_SOURCE_NO_ITEMS))
        {
            return etl::nullopt;
        }

        return GATAS::BarometricPressure{
            pressure.value() == UINT16_MAX || pressure.value() == 0
                ? GATAS::INVALID_PRESSSURE_HPA
                : static_cast<float>(pressure.value()) / 10.0f,
            static_cast<GATAS::PressureSource>(source.value())};
    }

    /**
     * Read Aircraft Position Info from a bit stream reader
     */
    static etl::optional<GATAS::AircraftPositionInfo> deserializeAircraftPositionV1(float ownshipLat, float ownshipLon,
                                                                                    etl::bit_stream_reader &reader)
    {
        if (!hasValidAircraftPositionSize(reader, 24U))
        {
            return etl::nullopt;
        }
        auto timeStamp = CoreUtils::timeUs32();
        auto type = reader.read_unchecked<uint8_t>(8U);
        if (type != DataType(DataType::AIRCRAFT_POSITION_TYPE_V1).get_value())
        {
            return etl::nullopt;
        }
        uint32_t addressRaw = reader.read_unchecked<uint32_t>(24U);
        uint8_t addressTypeIdx = reader.read_unchecked<uint8_t>(8U);
        uint8_t dataSourceIdx = reader.read_unchecked<uint8_t>(8U);
        float lat = static_cast<float>(reader.read_unchecked<int32_t>(32U)) / 1E7f;
        float lon = static_cast<float>(reader.read_unchecked<int32_t>(32U)) / 1E7f;
        int32_t heightHAE = static_cast<int32_t>(reader.read_unchecked<uint16_t>(16U)) - LEGACY_ELLIPSOID_HEIGHT_OFFSET_M;
        float track = static_cast<float>(reader.read_unchecked<uint8_t>(8U)) * (360.f / 255.f);
        float turnRate = static_cast<float>(reader.read_unchecked<int8_t>(8U)) / 5.0f;
        float groundSpeed = static_cast<float>(reader.read_unchecked<uint16_t>(16U)) / 100.f;
        float verticalRate = static_cast<float>(reader.read_unchecked<int16_t>(16U)) / 1024.f;
        uint8_t aircraftCategoryIdx = reader.read_unchecked<uint8_t>(8U);

        uint8_t callSignLen = etl::min(GATAS::MAX_CALLSIGN_LENGTH, reader.read_unchecked<uint8_t>(8U));
        char callSignBuffer[GATAS::MAX_CALLSIGN_LENGTH + 1] = {0};
        for (int i = 0; i < callSignLen; ++i)
        {
            callSignBuffer[i] = static_cast<char>(reader.read_unchecked<uint8_t>(8));
        }
        auto rel = CoreUtils::getDistanceRelNorthRelEastInt(ownshipLat, ownshipLon, lat, lon);

        return GATAS::AircraftPositionInfo(
            timeStamp,
            GATAS::CallSign(callSignBuffer),
            static_cast<GATAS::AircraftAddress>(addressRaw),
            static_cast<GATAS::AddressType>(addressTypeIdx),
            static_cast<GATAS::DataSource>(dataSourceIdx),
            static_cast<GATAS::AircraftCategory>(aircraftCategoryIdx),
            false, // stealth
            false, // noTrack
            true,  // airborne
            lat,
            lon,
            heightHAE,
            verticalRate,
            groundSpeed,
            track,
            turnRate,
            rel.distance);
    }

    /**
     * Read Aircraft Position Info from a bit stream reader
     */
    static etl::optional<GATAS::AircraftPositionInfo> deserializeAircraftPositionV2(float ownshipLat, float ownshipLon,
                                                                                    etl::bit_stream_reader &reader)
    {
        if (!hasValidAircraftPositionSize(reader, 28U))
        {
            return etl::nullopt;
        }
        auto type = reader.read_unchecked<uint8_t>(8U);
        if (type != DataType(DataType::AIRCRAFT_POSITION_TYPE_V2).get_value())
        {
            return etl::nullopt;
        }
        uint16_t msInMinute = reader.read_unchecked<uint16_t>(16U);
        auto timeStamp = CoreUtils::timeUs32FromMsInMinute(msInMinute);
        if (!timeStamp.has_value())
        {
            return etl::nullopt;
        }
        uint32_t addressRaw = reader.read_unchecked<uint32_t>(24U);
        uint8_t addressTypeIdx = reader.read_unchecked<uint8_t>(8U);
        uint8_t dataSourceIdx = reader.read_unchecked<uint8_t>(8U);
        float lat = static_cast<float>(reader.read_unchecked<int32_t>(32U)) / 1E7f;
        float lon = static_cast<float>(reader.read_unchecked<int32_t>(32U)) / 1E7f;
        int32_t heightHAE = static_cast<int32_t>(reader.read_unchecked<uint16_t>(16U)) - LEGACY_ELLIPSOID_HEIGHT_OFFSET_M;
        float track = static_cast<float>(reader.read_unchecked<uint8_t>(8U)) * (360.f / 255.f);
        float turnRate = static_cast<float>(reader.read_unchecked<int8_t>(8U)) / 5.0f;
        float groundSpeed = static_cast<float>(reader.read_unchecked<uint16_t>(16U)) / 100.f;
        float verticalRate = static_cast<float>(reader.read_unchecked<int16_t>(16U)) / 1024.f;
        uint8_t aircraftCategoryIdx = reader.read_unchecked<uint8_t>(8U);
        int16_t squawk = reader.read_unchecked<int16_t>(16U);

        uint8_t callSignLen = etl::min(GATAS::MAX_CALLSIGN_LENGTH, reader.read_unchecked<uint8_t>(8U));
        char callSignBuffer[GATAS::MAX_CALLSIGN_LENGTH + 1] = {0};
        for (int i = 0; i < callSignLen; ++i)
        {
            callSignBuffer[i] = static_cast<char>(reader.read_unchecked<uint8_t>(8));
        }
        auto rel = CoreUtils::getDistanceRelNorthRelEastInt(ownshipLat, ownshipLon, lat, lon);

        return GATAS::AircraftPositionInfo(
            timeStamp.value(),
            GATAS::CallSign(callSignBuffer),
            static_cast<GATAS::AircraftAddress>(addressRaw),
            static_cast<GATAS::AddressType>(addressTypeIdx),
            static_cast<GATAS::DataSource>(dataSourceIdx),
            static_cast<GATAS::AircraftCategory>(aircraftCategoryIdx),
            false,                                                // stealth
            false,                                                // noTrack
            groundSpeed > GATAS::GROUNDSPEED_CONSIDERING_AIRBORN, // airborne
            lat,
            lon,
            heightHAE,
            verticalRate,
            groundSpeed,
            track,
            turnRate,
            rel.distance,
            squawk);
    }

    static etl::optional<GATAS::AircraftPositionInfo> deserializeAircraftPositionV3(float ownshipLat, float ownshipLon,
                                                                                    etl::bit_stream_reader &reader)
    {
        if (!hasValidAircraftPositionSize(reader, 32U))
        {
            return etl::nullopt;
        }
        auto type = reader.read_unchecked<uint8_t>(8U);
        if (type != DataType(DataType::AIRCRAFT_POSITION_TYPE_V3).get_value())
        {
            return etl::nullopt;
        }
        uint16_t msInMinute = reader.read_unchecked<uint16_t>(16U);
        auto timeStamp = CoreUtils::timeUs32FromMsInMinute(msInMinute);
        if (!timeStamp.has_value())
        {
            return etl::nullopt;
        }
        uint32_t addressRaw = reader.read_unchecked<uint32_t>(24U);
        uint8_t addressTypeIdx = reader.read_unchecked<uint8_t>(8U);
        uint8_t dataSourceIdx = reader.read_unchecked<uint8_t>(8U);
        float lat = static_cast<float>(reader.read_unchecked<int32_t>(32U)) / 1E7f;
        float lon = static_cast<float>(reader.read_unchecked<int32_t>(32U)) / 1E7f;
        int32_t heightHAE = static_cast<int32_t>(reader.read_unchecked<uint16_t>(16U)) - V3_ELLIPSOID_HEIGHT_OFFSET_M;
        float track = static_cast<float>(reader.read_unchecked<uint8_t>(8U)) * (360.f / 255.f);
        float turnRate = static_cast<float>(reader.read_unchecked<int8_t>(8U)) / 5.0f;
        float groundSpeed = static_cast<float>(reader.read_unchecked<uint16_t>(16U)) / 100.f;
        float verticalRate = static_cast<float>(reader.read_unchecked<int16_t>(16U)) / 1024.f;
        uint8_t aircraftCategoryIdx = reader.read_unchecked<uint8_t>(8U);
        int16_t squawk = reader.read_unchecked<int16_t>(16U);
        uint16_t pressureAltitudeRaw = reader.read_unchecked<uint16_t>(16U);
        int32_t pressureAltitude = pressureAltitudeRaw == 0xFFFFU
                                       ? GATAS::INVALID_BARO_ALTITUDE
                                       : static_cast<int32_t>(pressureAltitudeRaw) - PRESSURE_ALTITUDE_OFFSET_M;
        // Keep the wire QNH field for compatibility; nearby traffic uses local QNH when needed.
        (void)reader.read_unchecked<uint16_t>(16U);

        uint8_t callSignLen = etl::min(GATAS::MAX_CALLSIGN_LENGTH, reader.read_unchecked<uint8_t>(8U));
        char callSignBuffer[GATAS::MAX_CALLSIGN_LENGTH + 1] = {0};
        for (int i = 0; i < callSignLen; ++i)
        {
            callSignBuffer[i] = static_cast<char>(reader.read_unchecked<uint8_t>(8));
        }
        auto rel = CoreUtils::getDistanceRelNorthRelEastInt(ownshipLat, ownshipLon, lat, lon);

        return GATAS::AircraftPositionInfo(
            timeStamp.value(),
            GATAS::CallSign(callSignBuffer),
            static_cast<GATAS::AircraftAddress>(addressRaw),
            static_cast<GATAS::AddressType>(addressTypeIdx),
            static_cast<GATAS::DataSource>(dataSourceIdx),
            static_cast<GATAS::AircraftCategory>(aircraftCategoryIdx),
            false,
            false,
            groundSpeed > GATAS::GROUNDSPEED_CONSIDERING_AIRBORN,
            lat,
            lon,
            heightHAE,
            verticalRate,
            groundSpeed,
            track,
            turnRate,
            rel.distance,
            squawk,
            pressureAltitude);
    }

    /**
     * Create a bitstream from an OwnshipPositionInfo to be send to gatasServer witg a requets
     * to send back aircraft
     */
    static void serializeOwnshipPosition(etl::bit_stream_writer &writer,
                                         const GATAS::OwnshipPositionInfo &ownship,
                                         DataType::enum_type requestType = DataType::AIRCRAFT_POSITION_REQUEST_V1)
    {
        writer.write_unchecked(DataType(requestType).get_value(), 8U);
        writer.write_unchecked(CoreUtils::secondsSinceEpoch(), 32U);
        writer.write_unchecked(ownship.conspicuity.icaoAddress, 24U);
        writer.write_unchecked(static_cast<uint8_t>(ownship.conspicuity.addressType), 8U);
        writer.write_unchecked(GATAS::AircraftCategory(ownship.conspicuity.category).get_value(), 8U);
        writer.write_unchecked(static_cast<int32_t>(std::round(ownship.lat * 1E7f)), 32U);
        writer.write_unchecked(static_cast<int32_t>(std::round(ownship.lon * 1E7f)), 32U);
        writer.write_unchecked(ownship.ellipseHeight + LEGACY_ELLIPSOID_HEIGHT_OFFSET_M, 16U);
        writer.write_unchecked(static_cast<uint8_t>(ownship.track / (360.f / 255.f)), 8U);
        writer.write_unchecked(static_cast<int8_t>(ownship.hTurnRate * 5.0f), 8U);
        writer.write_unchecked(static_cast<uint16_t>(ownship.groundSpeed * 10.f), 16U);
        writer.write_unchecked(static_cast<int16_t>(ownship.verticalSpeed * 100.f), 16U);
    }

    static void serializeOwnshipPositionV1(etl::bit_stream_writer &writer, const GATAS::OwnshipPositionInfo &ownship)
    {
        serializeOwnshipPosition(writer, ownship);
    }

    static void serializeOwnshipPositionV2(etl::bit_stream_writer &writer, const GATAS::OwnshipPositionInfo &ownship)
    {
        serializeOwnshipPosition(writer, ownship, DataType::AIRCRAFT_POSITION_REQUEST_V2);
        writer.write_unchecked(3U, 8U); // Request aircraft position response V3 with pressure altitude and QNH.
    }

    static size_t serializeOwnshipPositionV1(uint8_t *out, size_t outSize, const GATAS::OwnshipPositionInfo &ownship)
    {
        const size_t rawSize = serializeOwnshipPositionSizeV1().items(1);
        const size_t framedSize = serializeOwnshipPositionFramedSizeV1();
        if (outSize < framedSize || rawSize > MAX_COBS_FRAME_SIZE)
        {
            return 0;
        }

        uint8_t rawBuffer[MAX_COBS_FRAME_SIZE];
        etl::bit_stream_writer writer(rawBuffer, rawSize, etl::endian::big);
        serializeOwnshipPositionV1(writer, ownship);
        return encodeCOBS(rawBuffer, rawSize, out, outSize, true);
    }

    static size_t serializeOwnshipPositionV2(uint8_t *out, size_t outSize, const GATAS::OwnshipPositionInfo &ownship)
    {
        const size_t rawSize = serializeOwnshipPositionSizeV2().items(1);
        const size_t framedSize = serializeOwnshipPositionFramedSizeV2();
        if (outSize < framedSize || rawSize > MAX_COBS_FRAME_SIZE)
        {
            return 0;
        }

        uint8_t rawBuffer[MAX_COBS_FRAME_SIZE];
        etl::bit_stream_writer writer(rawBuffer, rawSize, etl::endian::big);
        serializeOwnshipPositionV2(writer, ownship);
        return encodeCOBS(rawBuffer, rawSize, out, outSize, true);
    }

    constexpr static BinaryMessages::SizeType serializeOwnshipPositionSizeV1()
    {
        size_t size = 1 + 4 + 3 + 1 + 1 + 4 + 4 + 2 + 1 + 1 + 2 + 2;
        return BinaryMessages::SizeType{
            .base = 0,
            .size = size};
    }

    constexpr static BinaryMessages::SizeType serializeOwnshipPositionSizeV2()
    {
        return BinaryMessages::SizeType{
            .base = 0,
            .size = serializeOwnshipPositionSizeV1().items(1) + 1};
    }

    static size_t serializeOwnshipPositionFramedSizeV1()
    {
        return getCOBSBufferSize(serializeOwnshipPositionSizeV1().items(1), true);
    }

    static size_t serializeOwnshipPositionFramedSizeV2()
    {
        return getCOBSBufferSize(serializeOwnshipPositionSizeV2().items(1), true);
    }

    static void serializeAircraftConfigurationV2(etl::bit_stream_writer &writer, uint32_t gatasId, uint32_t icaoAddressSnap, const etl::span<uint32_t> &addresses, uint32_t gatasIp, uint32_t pinCode, GATAS::WifiMode wifiMode, uint8_t bootId, bool dirtyFlag)
    {
        writer.write_unchecked(DataType(DataType::AIRCRAFT_CONFIGURATIONS_V2).get_value(), 8U);
        const uint8_t flags = (static_cast<uint8_t>(wifiMode) & AIRCRAFT_CONFIGURATION_WIFI_MODE_MASK)
            | (dirtyFlag ? AIRCRAFT_CONFIGURATION_DIRTY_FLAG : 0U);
        writer.write_unchecked(flags, 8U); // Bits 0-1: WiFi mode NC/AP/CLIENT; bit 2: dirty; bits 3-7: reserved
        writer.write_unchecked(gatasId, 32U);
        writer.write_unchecked(gatasIp, 32U);
        writer.write_unchecked(icaoAddressSnap, 24U);

        writer.write_unchecked<uint8_t>(3); // GATAS::VERSION_MAJOR
        writer.write_unchecked<uint8_t>(6); // GATAS::VERSION_MINOR
        writer.write_unchecked<uint8_t>(0); // GATAS::VERSION_BUGFIX
        writer.write_unchecked<uint8_t>(bootId);
        writer.write_unchecked(pinCode, 24U); // gatasConnect Pincode

//        GATAS_WARN("VERSION: %d %d %d", GATAS::VERSION_MAJOR, GATAS::VERSION_MINOR, GATAS::VERSION_BUGFIX);

        // options how to set the addres as a response
        // Examples could be:
        // 0 Not allowed at all
        // 1 Only set by user request, ia app or via website
        // 2 By automation
        writer.write_unchecked(0, 8U);

        writer.write_unchecked(addresses.size(), 8U);
        for (auto &addr : addresses)
        {
            writer.write_unchecked(addr, 24U);
        }
    }

    static size_t serializeAircraftConfigurationV2(uint8_t *out, size_t outSize, uint32_t gatasId, uint32_t icaoAddressSnap, const etl::span<uint32_t> &addresses, uint32_t gatasIp, uint32_t pinCode, GATAS::WifiMode wifiMode, uint8_t bootId, bool dirtyFlag)
    {
        const size_t rawSize = serializeAircraftConfigurationSizeV2().items(addresses.size());
        const size_t framedSize = serializeAircraftConfigurationFramedSizeV2(addresses.size());
        if (outSize < framedSize || rawSize > MAX_COBS_FRAME_SIZE)
        {
            return 0;
        }

        uint8_t rawBuffer[MAX_COBS_FRAME_SIZE];
        etl::bit_stream_writer writer(rawBuffer, rawSize, etl::endian::big);
        serializeAircraftConfigurationV2(writer, gatasId, icaoAddressSnap, addresses, gatasIp, pinCode, wifiMode, bootId, dirtyFlag);
        return encodeCOBS(rawBuffer, rawSize, out, outSize, true);
    }

    constexpr static BinaryMessages::SizeType serializeAircraftConfigurationSizeV2()
    {
        return BinaryMessages::SizeType{
            .base = 1 + 1 + 4 + 4 + 3 + 4 + 3 + 1 + 1, // By default we will use 4 bytes 10
            .size = 3                                  // For each additional item 3 bytes
        };
    }

    static size_t serializeAircraftConfigurationFramedSizeV2(size_t items)
    {
        return getCOBSBufferSize(serializeAircraftConfigurationSizeV2().items(items), true);
    }

    constexpr static BinaryMessages::SizeType serializeGdl90SizeV1()
    {
        return BinaryMessages::SizeType{
            .base = 1,
            .size = 1};
    }

    static size_t serializeGdl90FramedSizeV1(size_t items)
    {
        return getCOBSBufferSize(serializeGdl90SizeV1().items(items), true);
    }

    static size_t serializeGdl90V1(uint8_t *out, size_t outSize, const etl::span<const uint8_t> &gdl90Message)
    {
        const size_t rawSize = serializeGdl90SizeV1().items(gdl90Message.size());
        const size_t framedSize = serializeGdl90FramedSizeV1(gdl90Message.size());
        if (outSize < framedSize)
        {
            return 0;
        }

        uint8_t rawBuffer[MAX_COBS_FRAME_SIZE];
        if (rawSize > sizeof(rawBuffer))
        {
            return 0;
        }

        rawBuffer[0] = DataType(DataType::GDL90_V1).get_value();
        etl::copy(gdl90Message.begin(), gdl90Message.end(), rawBuffer + 1);
        return encodeCOBS(rawBuffer, rawSize, out, outSize, true);
    }

    static uint32_t deserializeSetIcaoAddressV1(etl::bit_stream_reader &reader)
    {
        if (reader.size_bytes() != 4U)
        {
            return 0x00;
        }
        const auto type = reader.read<uint8_t>();
        const auto address = reader.read<uint32_t>(24U);
        if (!type || !address || type.value() != DataType(DataType::SET_ICAO_ADDRESS_V1).get_value())
        {
            return 0x00;
        }
        return address.value();
    }

    /**
     * Deserialize the SET_WIFI_MODE_V1 message. THis wil ignore the WifiMode::NC and return false
     * @param reader
     * @param wifiMode
     * @return
     */
    static bool deserializeSetWifiModeV1(etl::bit_stream_reader &reader, GATAS::WifiMode &wifiMode)
    {
        if (reader.size_bytes() != 2U)
        {
            return false;
        }
        const auto type = reader.read<uint8_t>();
        const auto modeValue = reader.read<uint8_t>();
        if (!type || !modeValue || type.value() != DataType(DataType::SET_WIFI_MODE_V1).get_value())
        {
            return false;
        }

        const auto mode = modeValue.value();
        if (mode == GATAS::WifiMode::AP)
        {
            wifiMode = GATAS::WifiMode::AP;
            return true;
        }

        if (mode == GATAS::WifiMode::CLIENT)
        {
            wifiMode = GATAS::WifiMode::CLIENT;
            return true;
        }

        return false;
    }

    /**
     *
     */
    static GATAS::AircraftCategory safeMapAircraftCategoryToType(uint8_t category)
    {
        // won't work well due to not mapping to unknown correctly
        // return GATAS::AircraftCategory(category);
        // clang-format off
        switch (category)
        {
            // Standard categories
            case 0:  return GATAS::AircraftCategory::UNKNOWN;
            case 1:  return GATAS::AircraftCategory::LIGHT;
            case 2:  return GATAS::AircraftCategory::SMALL;
            case 3:  return GATAS::AircraftCategory::LARGE;
            case 4:  return GATAS::AircraftCategory::HIGH_VORTEX;
            case 5:  return GATAS::AircraftCategory::HEAVY_ICAO;
            case 6:  return GATAS::AircraftCategory::AEROBATIC;
            case 7:  return GATAS::AircraftCategory::ROTORCRAFT;
            case 9:  return GATAS::AircraftCategory::GLIDER;
            case 10: return GATAS::AircraftCategory::LIGHT_THAN_AIR;
            case 11: return GATAS::AircraftCategory::SKY_DIVER;
            case 12: return GATAS::AircraftCategory::ULTRA_LIGHT_FIXED_WING;
            case 14: return GATAS::AircraftCategory::UN_MANNED;
            case 15: return GATAS::AircraftCategory::SPACE_VEHICLE;
            case 17: return GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE;
            case 18: return GATAS::AircraftCategory::SURFACE_VEHICLE;
            case 19: return GATAS::AircraftCategory::POINT_OBSTACLE;
            case 20: return GATAS::AircraftCategory::CLUSTER_OBSTACLE;
            case 21: return GATAS::AircraftCategory::LINE_OBSTACLE;
            case 40: return GATAS::AircraftCategory::GYROCOPTER;
            case 41: return GATAS::AircraftCategory::HANG_GLIDER;
            case 42: return GATAS::AircraftCategory::PARA_GLIDER;
            case 43: return GATAS::AircraftCategory::DROP_PLANE;
            case 44: return GATAS::AircraftCategory::MILITARY;
            case 8:
            case 13:
            case 16:
            default:return GATAS::AircraftCategory::UNKNOWN;
        }
        // clang-format on
    }

    static GATAS::AircraftCategory mapAircraftCategoryToType(const etl::string_view category)
    {
        // Map using ETL_ENUM_TYPE string representations
        // clang-format off
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::UNKNOWN).c_str()) return GATAS::AircraftCategory::UNKNOWN;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::LIGHT).c_str()) return GATAS::AircraftCategory::LIGHT;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::SMALL).c_str()) return GATAS::AircraftCategory::SMALL;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::LARGE).c_str()) return GATAS::AircraftCategory::LARGE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::HIGH_VORTEX).c_str()) return GATAS::AircraftCategory::HIGH_VORTEX;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::HEAVY_ICAO).c_str()) return GATAS::AircraftCategory::HEAVY_ICAO;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::AEROBATIC).c_str()) return GATAS::AircraftCategory::AEROBATIC;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::ROTORCRAFT).c_str()) return GATAS::AircraftCategory::ROTORCRAFT;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::GLIDER).c_str()) return GATAS::AircraftCategory::GLIDER;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::LIGHT_THAN_AIR).c_str()) return GATAS::AircraftCategory::LIGHT_THAN_AIR;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::SKY_DIVER).c_str()) return GATAS::AircraftCategory::SKY_DIVER;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::ULTRA_LIGHT_FIXED_WING).c_str()) return GATAS::AircraftCategory::ULTRA_LIGHT_FIXED_WING;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::UN_MANNED).c_str()) return GATAS::AircraftCategory::UN_MANNED;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::SPACE_VEHICLE).c_str()) return GATAS::AircraftCategory::SPACE_VEHICLE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE).c_str()) return GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::SURFACE_VEHICLE).c_str()) return GATAS::AircraftCategory::SURFACE_VEHICLE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::POINT_OBSTACLE).c_str()) return GATAS::AircraftCategory::POINT_OBSTACLE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::CLUSTER_OBSTACLE).c_str()) return GATAS::AircraftCategory::CLUSTER_OBSTACLE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::LINE_OBSTACLE).c_str()) return GATAS::AircraftCategory::LINE_OBSTACLE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::GYROCOPTER).c_str()) return GATAS::AircraftCategory::GYROCOPTER;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::HANG_GLIDER).c_str()) return GATAS::AircraftCategory::HANG_GLIDER;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::PARA_GLIDER).c_str()) return GATAS::AircraftCategory::PARA_GLIDER;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::DROP_PLANE).c_str()) return GATAS::AircraftCategory::DROP_PLANE;
        if (category == GATAS::AircraftCategory(GATAS::AircraftCategory::MILITARY).c_str()) return GATAS::AircraftCategory::MILITARY;
        // clang-format on

        // Fallback to numeric value
        if (isdigit(category.front()))
        {
            return safeMapAircraftCategoryToType(etl::to_arithmetic<uint8_t>(category));
        }

        return GATAS::AircraftCategory::UNKNOWN;
    }

    /**
     * Generate a checksuom for Bimnary Messages
     */
    static uint16_t binaryMsgChecksum(etl::span<uint8_t> packet)
    {
        uint16_t crc16 = 0xffff;
        for (auto byte : packet)
        {
            crc16 = update_crc_ccitt(crc16, byte);
        }

        return crc16;
    }
};
