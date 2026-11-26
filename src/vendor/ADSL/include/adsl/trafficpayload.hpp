#pragma once

#include <stdint.h>
#include "utils.hpp"
#include "header.hpp"

#include "etl/algorithm.h"
#include "etl/bit_stream.h"

namespace ADSL
{
  /**
   * Tracking payload
   * Messagetype : 1
   */
  class TrafficPayload final
  {
  public:
    static constexpr size_t LENGTH = 15;

    // Enums for encoded fields
    struct FlightState
    {
      enum enum_type : uint8_t
      {
        UNDEFINED = 0,
        ON_GROUND = 1,
        AIRBORNE = 2,
        RESERVED = 3
      };

      ETL_DECLARE_ENUM_TYPE(FlightState, uint8_t)
      ETL_ENUM_TYPE(UNDEFINED, "UNDEFINED")
      ETL_ENUM_TYPE(ON_GROUND, "ON_GROUND")
      ETL_ENUM_TYPE(AIRBORNE, "AIRBORNE")
      ETL_ENUM_TYPE(RESERVED, "RESERVED")
      ETL_END_ENUM_TYPE
    };

    struct AircraftCategory
    {
      enum enum_type : uint8_t
      {
        NO_INFO = 0,
        LIGHT_FIXED_WING = 1,          // < 7031 kg / 15,500 lbs
        SMALL_TO_HEAVY_FIXED_WING = 2, // ≥ 7031 kg / 15,500 lbs
        LIGHT_ROTORCRAFT = 3,
        GLIDER_SAILPLANE = 4,
        LIGHTER_THAN_AIR = 5,
        ULTRALIGHT_HANGGLIDER = 6,
        PARAGLIDER = 7,
        PARACHUTIST = 8,
        EVTOL_UAM = 9,
        GYROCOPTER = 10,
        UAS_OPEN_CATEGORY = 11,
        UAS_SPECIFIC_CATEGORY = 12,
        UAS_CERTIFIED_CATEGORY = 13,
        MODEL_PLANE = 14,
        HEAVY_ROTORCRAFT = 15,
        HANGGLIDER = 16,
        PARAMOTOR = 17,
        // 18-31: Reserved
        STATIC_OBJECT = 31 // Reserved by ADS-L; do not transmit this value.
      };

      ETL_DECLARE_ENUM_TYPE(AircraftCategory, uint8_t)
      ETL_ENUM_TYPE(NO_INFO, "NO_INFO")
      ETL_ENUM_TYPE(LIGHT_FIXED_WING, "LIGHT_FIXED_WING")
      ETL_ENUM_TYPE(SMALL_TO_HEAVY_FIXED_WING, "SMALL_TO_HEAVY_FIXED_WING")
      ETL_ENUM_TYPE(LIGHT_ROTORCRAFT, "LIGHT_ROTORCRAFT")
      ETL_ENUM_TYPE(GLIDER_SAILPLANE, "GLIDER_SAILPLANE")
      ETL_ENUM_TYPE(LIGHTER_THAN_AIR, "LIGHTER_THAN_AIR")
      ETL_ENUM_TYPE(ULTRALIGHT_HANGGLIDER, "ULTRALIGHT_HANGGLIDER")
      ETL_ENUM_TYPE(PARAGLIDER, "PARAGLIDER")
      ETL_ENUM_TYPE(PARACHUTIST, "PARACHUTIST")
      ETL_ENUM_TYPE(EVTOL_UAM, "EVTOL_UAM")
      ETL_ENUM_TYPE(GYROCOPTER, "GYROCOPTER")
      ETL_ENUM_TYPE(UAS_OPEN_CATEGORY, "UAS_OPEN_CATEGORY")
      ETL_ENUM_TYPE(UAS_SPECIFIC_CATEGORY, "UAS_SPECIFIC_CATEGORY")
      ETL_ENUM_TYPE(UAS_CERTIFIED_CATEGORY, "UAS_CERTIFIED_CATEGORY")
      ETL_ENUM_TYPE(MODEL_PLANE, "MODEL_PLANE")
      ETL_ENUM_TYPE(HEAVY_ROTORCRAFT, "HEAVY_ROTORCRAFT")
      ETL_ENUM_TYPE(HANGGLIDER, "HANGGLIDER")
      ETL_ENUM_TYPE(PARAMOTOR, "PARAMOTOR")
      ETL_ENUM_TYPE(STATIC_OBJECT, "STATIC_OBJECT")
      ETL_END_ENUM_TYPE
    };

    struct EmergencyStatus
    {
      enum enum_type : uint8_t
      {
        UNDEFINED = 0,
        NO_EMERGENCY = 1,
        GENERAL_EMERGENCY = 2,
        LIFEGUARD_MEDICAL = 3,
        NO_COMMUNICATIONS = 4,
        UNLAWFUL_INTERFERENCE = 5,
        DOWNED_AIRCRAFT = 6,
        RESERVED = 7
      };
      ETL_DECLARE_ENUM_TYPE(EmergencyStatus, uint8_t)
      ETL_ENUM_TYPE(UNDEFINED, "UNDEFINED")
      ETL_ENUM_TYPE(NO_EMERGENCY, "NO_EMERGENCY")
      ETL_ENUM_TYPE(GENERAL_EMERGENCY, "GENERAL_EMERGENCY")
      ETL_ENUM_TYPE(LIFEGUARD_MEDICAL, "LIFEGUARD_MEDICAL")
      ETL_ENUM_TYPE(NO_COMMUNICATIONS, "NO_COMMUNICATIONS")
      ETL_ENUM_TYPE(UNLAWFUL_INTERFERENCE, "UNLAWFUL_INTERFERENCE")
      ETL_ENUM_TYPE(DOWNED_AIRCRAFT, "DOWNED_AIRCRAFT")
      ETL_ENUM_TYPE(RESERVED, "RESERVED")
      ETL_END_ENUM_TYPE
    };

    struct SourceIntegrityLevel
    {
      enum enum_type : uint8_t
      {
        UNDEFINED_SIL = 0, // Undefined or SIL > 1E-3 per flight hour
        SIL_1E3 = 1,       // 1E-5 < SIL ≤ 1E-3 per flight hour
        SIL_1E5 = 2,       // 1E-7 < SIL ≤ 1E-5 per flight hour
        SIL_1E7 = 3        // SIL ≤ 1E-7 per flight hour
      };
      ETL_DECLARE_ENUM_TYPE(SourceIntegrityLevel, uint8_t)
      ETL_ENUM_TYPE(UNDEFINED_SIL, "UNDEFINED_SIL")
      ETL_ENUM_TYPE(SIL_1E3, "SIL_1E3")
      ETL_ENUM_TYPE(SIL_1E5, "SIL_1E5")
      ETL_ENUM_TYPE(SIL_1E7, "SIL_1E7")
      ETL_END_ENUM_TYPE
    };

    struct DesignAssurance
    {
      enum enum_type : uint8_t
      {
        UNDEFINED_NONE = 0,
        DAL_D = 1,
        DAL_C = 2,
        DAL_B = 3
      };
      ETL_DECLARE_ENUM_TYPE(DesignAssurance, uint8_t)
      ETL_ENUM_TYPE(UNDEFINED_NONE, "UNDEFINED_NONE")
      ETL_ENUM_TYPE(DAL_D, "DAL_D")
      ETL_ENUM_TYPE(DAL_C, "DAL_C")
      ETL_ENUM_TYPE(DAL_B, "DAL_B")
      ETL_END_ENUM_TYPE
    };

    struct NavigationIntegrity
    {
      enum enum_type : uint8_t
      {
        UNDEFINED = 0,
        RC_20NM_OR_MORE = 1,
        RC_8_20NM = 2,
        RC_4_8NM = 3,
        RC_2_4NM = 4,
        RC_1_2NM = 5,
        RC_0_6_1NM = 6,
        RC_0_2_0_6NM = 7,
        RC_0_1_0_2NM = 8,
        RC_75M_0_1NM = 9,
        RC_25_75M = 10,
        RC_7_5_25M = 11,
        RC_LESS_7_5M = 12
        // 13-15: Reserved
      };
      ETL_DECLARE_ENUM_TYPE(NavigationIntegrity, uint8_t)
      ETL_ENUM_TYPE(UNDEFINED, "UNDEFINED")
      ETL_ENUM_TYPE(RC_20NM_OR_MORE, "RC_20NM_OR_MORE")
      ETL_ENUM_TYPE(RC_8_20NM, "RC_8_20NM")
      ETL_ENUM_TYPE(RC_4_8NM, "RC_4_8NM")
      ETL_ENUM_TYPE(RC_2_4NM, "RC_2_4NM")
      ETL_ENUM_TYPE(RC_1_2NM, "RC_1_2NM")
      ETL_ENUM_TYPE(RC_0_6_1NM, "RC_0_6_1NM")
      ETL_ENUM_TYPE(RC_0_2_0_6NM, "RC_0_2_0_6NM")
      ETL_ENUM_TYPE(RC_0_1_0_2NM, "RC_0_1_0_2NM")
      ETL_ENUM_TYPE(RC_75M_0_1NM, "RC_75M_0_1NM")
      ETL_ENUM_TYPE(RC_25_75M, "RC_25_75M")
      ETL_ENUM_TYPE(RC_7_5_25M, "RC_7_5_25M")
      ETL_ENUM_TYPE(RC_LESS_7_5M, "RC_LESS_7_5M")
      ETL_END_ENUM_TYPE
    };

    struct HorizontalPositionAccuracy
    {
      enum enum_type : uint8_t
      {
        UNKNOWN_NO_FIX = 0,  // HFOM ≥ 0.5 NM
        HFOM_0_3_0_5NM = 1,  // 0.3 NM ≤ HFOM < 0.5 NM
        HFOM_0_1_0_3NM = 2,  // 0.1 NM ≤ HFOM < 0.3 NM
        HFOM_0_05_0_1NM = 3, // 0.05 NM ≤ HFOM < 0.1 NM
        HFOM_30M_0_05NM = 4, // 30 m ≤ HFOM < 0.05 NM
        HFOM_10_30M = 5,     // 10 m ≤ HFOM < 30 m
        HFOM_3_10M = 6,      // 3 m ≤ HFOM < 10 m
        HFOM_LESS_3M = 7     // HFOM < 3 m
      };
      ETL_DECLARE_ENUM_TYPE(HorizontalPositionAccuracy, uint8_t)
      ETL_ENUM_TYPE(UNKNOWN_NO_FIX, "UNKNOWN_NO_FIX")
      ETL_ENUM_TYPE(HFOM_0_3_0_5NM, "HFOM_0_3_0_5NM")
      ETL_ENUM_TYPE(HFOM_0_1_0_3NM, "HFOM_0_1_0_3NM")
      ETL_ENUM_TYPE(HFOM_0_05_0_1NM, "HFOM_0_05_0_1NM")
      ETL_ENUM_TYPE(HFOM_30M_0_05NM, "HFOM_30M_0_05NM")
      ETL_ENUM_TYPE(HFOM_10_30M, "HFOM_10_30M")
      ETL_ENUM_TYPE(HFOM_3_10M, "HFOM_3_10M")
      ETL_ENUM_TYPE(HFOM_LESS_3M, "HFOM_LESS_3M")
      ETL_END_ENUM_TYPE
    };

    struct VerticalPositionAccuracy
    {
      enum enum_type : uint8_t
      {
        UNKNOWN_NO_FIX = 0, // VFOM ≥ 150 m
        VFOM_45_150M = 1,   // 45 m ≤ VFOM < 150 m
        VFOM_10_45M = 2,    // 10 m ≤ VFOM < 45 m
        VFOM_LESS_10M = 3   // VFOM < 10 m
      };
      ETL_DECLARE_ENUM_TYPE(VerticalPositionAccuracy, uint8_t)
      ETL_ENUM_TYPE(UNKNOWN_NO_FIX, "UNKNOWN_NO_FIX")
      ETL_ENUM_TYPE(VFOM_45_150M, "VFOM_45_150M")
      ETL_ENUM_TYPE(VFOM_10_45M, "VFOM_10_45M")
      ETL_ENUM_TYPE(VFOM_LESS_10M, "VFOM_LESS_10M")
      ETL_END_ENUM_TYPE
    };

    struct VelocityAccuracy
    {
      enum enum_type : uint8_t
      {
        UNKNOWN_NO_FIX = 0,  // AccVel ≥ 10 m/s
        ACC_VEL_3_10MS = 1,  // 3 m/s ≤ AccVel < 10 m/s
        ACC_VEL_1_3MS = 2,   // 1 m/s ≤ AccVel < 3 m/s
        ACC_VEL_LESS_1MS = 3 // AccVel < 1 m/s
      };
      ETL_DECLARE_ENUM_TYPE(VelocityAccuracy, uint8_t)
      ETL_ENUM_TYPE(UNKNOWN_NO_FIX, "UNKNOWN_NO_FIX")
      ETL_ENUM_TYPE(ACC_VEL_3_10MS, "ACC_VEL_3_10MS")
      ETL_ENUM_TYPE(ACC_VEL_1_3MS, "ACC_VEL_1_3MS")
      ETL_ENUM_TYPE(ACC_VEL_LESS_1MS, "ACC_VEL_LESS_1MS")
      ETL_END_ENUM_TYPE
    };

    // Constants
    static constexpr int32_t INVALID_POSITION = -8'388'608; // 24-bit signed 0x800000 sentinel.
    static constexpr uint8_t INVALID_GROUND_SPEED = 0xFF;
    static constexpr uint16_t INVALID_ALTITUDE = 0x3FFF;
    static constexpr uint16_t INVALID_VERTICAL_RATE = 0x1FF;

  private:
    // Data members
    uint8_t timestamp_ = 0;                                                                              // 6 bits: 0-59 quarter seconds
    FlightState flightState_ = FlightState::ON_GROUND;                                                   // 2 bits
    AircraftCategory aircraftCategory_ = AircraftCategory::NO_INFO;                                      // 5 bits
    EmergencyStatus emergencyStatus_ = EmergencyStatus::NO_EMERGENCY;                                    // 3 bits
    int32_t latitude_ = INVALID_POSITION;                                                                // 24 bits: signed, LSB = 1°/93206
    int32_t longitude_ = INVALID_POSITION;                                                               // 24 bits: signed, LSB = 1°/46603
    uint8_t groundSpeed_ = INVALID_GROUND_SPEED;                                                         // 8 bits: exponentially encoded
    uint16_t altitude_ = INVALID_ALTITUDE;                                                               // 14 bits: exponentially encoded with offset
    int16_t verticalRate_ = INVALID_VERTICAL_RATE;                                                       // 9 bits: exponentially encoded, signed
    uint16_t groundTrack_ = 0;                                                                           // 9 bits: cyclic 0-511
    SourceIntegrityLevel sourceIntegrityLevel_ = SourceIntegrityLevel::UNDEFINED_SIL;                    // 2 bits
    DesignAssurance designAssurance_ = DesignAssurance::UNDEFINED_NONE;                                  // 2 bits
    NavigationIntegrity navigationIntegrity_ = NavigationIntegrity::UNDEFINED;                           // 4 bits
    HorizontalPositionAccuracy horizontalPositionAccuracy_ = HorizontalPositionAccuracy::UNKNOWN_NO_FIX; // 3 bits
    VerticalPositionAccuracy verticalPositionAccuracy_ = VerticalPositionAccuracy::UNKNOWN_NO_FIX;       // 2 bits
    VelocityAccuracy velocityAccuracy_ = VelocityAccuracy::UNKNOWN_NO_FIX;                               // 2 bits

  public:
    explicit TrafficPayload() = default;

    /**
     * @brief Get the message type.
     * @return The message type.
     */
    Header::PayloadTypeIdentifier type() const
    {
      return Header::PayloadTypeIdentifier::TRAFFIC;
    }

    void latitude(float lat) { latitude_ = (FloatToFNT(lat) + 0x40) >> 7; }

    float latitude() const
    {
      return FNTtoFloat(latitude_ * 128);
    }

    void longitude(float lon) { longitude_ = (FloatToFNT(lon) + 0x80) >> 8; }

    float longitude() const
    {
      return FNTtoFloat(longitude_ * 256);
    }

    void speed(float groundSpeed)
    {
      groundSpeed = etl::clamp(static_cast<uint16_t>(groundSpeed * 4.0f + 0.5f), static_cast<uint16_t>(0), static_cast<uint16_t>(236 * 4));
      groundSpeed_ = ADSL::UnsVRencode<uint16_t, 6>(groundSpeed);
    }

    float speed() const
    {
      return static_cast<float>(ADSL::UnsVRdecode<uint16_t, 6>(groundSpeed_)) / 4.0f;
    }

    void altitude(int32_t altitude)
    {
      altitude = etl::clamp(altitude, static_cast<int32_t>(-320), static_cast<int32_t>(61112));
      altitude_ = static_cast<uint16_t>(etl::min<uint32_t>(UnsVRencode<uint32_t, 12>(static_cast<uint32_t>(altitude + 320)), 0x3FFEu));
    }

    int32_t altitude() const
    {
      return UnsVRdecode<int32_t, 12>(altitude_) - 320;
    }

    void verticalRate(float verticalRate)
    {
      verticalRate = etl::clamp(static_cast<int16_t>(roundf(verticalRate * 8.f)), static_cast<int16_t>(-118 * 8), static_cast<int16_t>(119 * 8));
      verticalRate_ = SignVRencode<int16_t, 6>(verticalRate);
    }

    float verticalRate() const
    {
      float vr = static_cast<float>(SignVRdecode<int16_t, 6>(verticalRate_)) / 8.f;
      return vr;
    }

    void groundTrack(float track)
    {
      int32_t q = static_cast<int32_t>(track * (512.0f / 360.0f));

      // Wrap modulo 512
      q %= 512;
      if (q < 0)
      {
        q += 512;
      }

      groundTrack_ = static_cast<uint16_t>(q);
    }

    float groundTrack() const
    {
      return groundTrack_ * (360.0f / 512.0f);
    }

    /**
     * @brief Set the source integrity level.
     * Note: Don't set this see ADS-L.4.SRD860.G.1.17
     * @param level The source integrity level.
     */
    void sourceIntegrityLevel(SourceIntegrityLevel level)
    {
      sourceIntegrityLevel_ = level;
    }

    SourceIntegrityLevel sourceIntegrityLevel() const
    {
      return sourceIntegrityLevel_;
    }

    /**
     * @brief Set the design assurance.
     * Note: Don't set this see ADS-L.4.SRD860.G.1.17
     * @param level The source integrity level.
     */
    void designAssurance(DesignAssurance assurance)
    {
      designAssurance_ = assurance;
    }

    DesignAssurance designAssurance() const { return designAssurance_; }

    void navigationIntegrity(NavigationIntegrity integrity)
    {
      navigationIntegrity_ = integrity;
    }

    /**
     * @brief Set the navigation Integrity.
     * Note: Don't set this see ADS-L.4.SRD860.G.1.17
     * @param level The source integrity level.
     */
    NavigationIntegrity navigationIntegrity() const
    {
      return navigationIntegrity_;
    }

    /**
     * @brief Set the Horizontal  Position Accuracy.
     * Note: Don't set this see ADS-L.4.SRD860.G.1.17
     * @param level The Horizontal Position Accuracy.
     */
    void horizontalPositionAccuracy(HorizontalPositionAccuracy accuracy)
    {
      horizontalPositionAccuracy_ = accuracy;
    }

    HorizontalPositionAccuracy horizontalPositionAccuracy() const
    {
      return horizontalPositionAccuracy_;
    }

    /**
     * @brief Set the Vertical Position Accuracy
     * Note: Don't set this see ADS-L.4.SRD860.G.1.17
     * @param level The Vertical Position Accuracy
     */
    void verticalPositionAccuracy(VerticalPositionAccuracy accuracy)
    {
      verticalPositionAccuracy_ = accuracy;
    }

    VerticalPositionAccuracy verticalPositionAccuracy() const
    {
      return verticalPositionAccuracy_;
    }

    /**
     * @brief Set the Velocity Accuracy
     * Note: Don't set this see ADS-L.4.SRD860.G.1.17
     * @param level The Vertical Position Accuracy
     */
    void velocityAccuracy(VelocityAccuracy accuracy)
    {
      velocityAccuracy_ = accuracy;
    }

    VelocityAccuracy velocityAccuracy() const
    {
      return velocityAccuracy_;
    }

    void emergencyStatus(EmergencyStatus status)
    {
      emergencyStatus_ = status;
    }

    EmergencyStatus emergencyStatus() const
    {
      return emergencyStatus_;
    }

    void flightState(FlightState state) { flightState_ = state; }

    FlightState flightState() const
    {
      return flightState_;
    }

    void aircraftCategory(AircraftCategory category)
    {
      aircraftCategory_ = category;
    }

    AircraftCategory aircraftCategory() const
    {
      return aircraftCategory_;
    }

    void timestamp(uint64_t epochms)
    {
      timestamp_ = ((epochms / 250) % 60) & 0x3F;
    };

    uint8_t timestampQms() const
    {
      return timestamp_;
    }

    uint64_t timestampRestored(uint64_t epochms) const
    {
      const uint64_t nowTick = epochms / 250ULL;
      const uint64_t rxTick = timestampQms();

      const uint64_t base = nowTick - (nowTick % 60ULL);
      uint64_t candidate = base + rxTick;

      if (candidate > nowTick)
      {
        candidate -= 60ULL;
      }

      return candidate * 250ULL;
    }

    /**
     * @brief Serialize the tracking payload to a bit stream. Issue 1 and 2
     * @param writer The bit stream writer.
     */
    size_t serialize_issue1(etl::bit_stream_writer &writer) const
    {
      writer.write_unchecked(timestamp_, 6U);
      writer.write_unchecked(flightState_.get_value(), 2U);
      writer.write_unchecked(static_cast<uint8_t>(aircraftCategory_), 5U);
      writer.write_unchecked(static_cast<uint8_t>(emergencyStatus_), 3U);
      writer.write_unchecked(static_cast<uint32_t>(latitude_), 24U);
      writer.write_unchecked(static_cast<uint32_t>(longitude_), 24U);
      writer.write_unchecked(groundSpeed_, 8);
      writer.write_unchecked(static_cast<uint16_t>(altitude_), 14);
      writer.write_unchecked(static_cast<int16_t>(verticalRate_), 9);
      writer.write_unchecked(static_cast<uint16_t>(groundTrack_), 9);
      writer.write_unchecked(static_cast<uint8_t>(sourceIntegrityLevel_), 2);
      writer.write_unchecked(static_cast<uint8_t>(designAssurance_), 2);
      writer.write_unchecked(static_cast<uint8_t>(navigationIntegrity_), 4);
      writer.write_unchecked(static_cast<uint8_t>(horizontalPositionAccuracy_), 3);
      writer.write_unchecked(static_cast<uint8_t>(verticalPositionAccuracy_), 2);
      writer.write_unchecked(static_cast<uint8_t>(velocityAccuracy_), 2);
      writer.write_unchecked(0, 1);
      return TrafficPayload::LENGTH;
    }

    /**
     * @brief Deserialize the tracking payload from a bit stream. Issue 1 and 2
     * @param reader The bit stream reader.
     * @return The deserialized tracking payload.
     */
    static const TrafficPayload deserialize_issue1(etl::bit_stream_reader &reader)
    {
      TrafficPayload traffic;
      traffic.timestamp_ = reader.read_unchecked<uint8_t>(6U);
      traffic.flightState_ = FlightState(reader.read_unchecked<uint8_t>(2U));
      traffic.aircraftCategory_ = AircraftCategory(reader.read_unchecked<uint8_t>(5U));
      traffic.emergencyStatus_ = EmergencyStatus(reader.read_unchecked<uint8_t>(3U));
      const auto latitudeBits = reader.read_unchecked<uint32_t>(24U);
      const auto longitudeBits = reader.read_unchecked<uint32_t>(24U);
      traffic.latitude_ = (latitudeBits & 0x800000U) != 0U
                              ? static_cast<int32_t>(latitudeBits | 0xFF000000U)
                              : static_cast<int32_t>(latitudeBits);
      traffic.longitude_ = (longitudeBits & 0x800000U) != 0U
                               ? static_cast<int32_t>(longitudeBits | 0xFF000000U)
                               : static_cast<int32_t>(longitudeBits);
      traffic.groundSpeed_ = reader.read_unchecked<uint8_t>(8U);
      traffic.altitude_ = reader.read_unchecked<uint16_t>(14U);
      traffic.verticalRate_ = reader.read_unchecked<int16_t>(9U);
      traffic.groundTrack_ = reader.read_unchecked<uint16_t>(9U);
      traffic.sourceIntegrityLevel_ = SourceIntegrityLevel(reader.read_unchecked<uint8_t>(2U));
      traffic.designAssurance_ = DesignAssurance(reader.read_unchecked<uint8_t>(2U));
      traffic.navigationIntegrity_ = NavigationIntegrity(reader.read_unchecked<uint8_t>(4U));
      traffic.horizontalPositionAccuracy_ = HorizontalPositionAccuracy(reader.read_unchecked<uint8_t>(3U));
      traffic.verticalPositionAccuracy_ = VerticalPositionAccuracy(reader.read_unchecked<uint8_t>(2U));
      traffic.velocityAccuracy_ = VelocityAccuracy(reader.read_unchecked<uint8_t>(2U));
      reader.read_unchecked<uint8_t>(1);
      return traffic;
    }
  };
}; // namespace ADSL
