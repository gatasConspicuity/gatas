#pragma once

#include <stdint.h>

#include "header.hpp"
#include "networkpayload.hpp"

#include "etl/bit_stream.h"

namespace ADSL
{
  /**
   * Tracking payload
   * Messagetype : 1
   */
  class StatusPayload final
  {
  public:
    static constexpr size_t LENGTH = 7;

    struct ReceiveCapability
    {
      enum enum_type : uint8_t
      {
        None = 0,
        Occasional = 1,
        Partial = 2,
        Full = 3
      };

      ETL_DECLARE_ENUM_TYPE(ReceiveCapability, uint8_t)
      ETL_ENUM_TYPE(None, "None")
      ETL_ENUM_TYPE(Occasional, "Occasional")
      ETL_ENUM_TYPE(Partial, "Partial")
      ETL_ENUM_TYPE(Full, "Full")
      ETL_END_ENUM_TYPE
    };

    struct MobileState

    {
      enum enum_type : uint8_t
      {
        None = 0,
        Offline = 1,
        Online = 2,
        Reserved = 3
      };

      ETL_DECLARE_ENUM_TYPE(MobileState, uint8_t)
      ETL_ENUM_TYPE(None, "None")
      ETL_ENUM_TYPE(Offline, "Offline")
      ETL_ENUM_TYPE(Online, "Online")
      ETL_ENUM_TYPE(Reserved, "Reserved")
      ETL_END_ENUM_TYPE
    };

    struct XpdrCapability
    {
      enum enum_type : uint8_t
      {
        NoneOrOff = 0,
        ModeC = 1,
        ModeS = 2,
        Reserved = 3
      };

      ETL_DECLARE_ENUM_TYPE(XpdrCapability, uint8_t)
      ETL_ENUM_TYPE(NoneOrOff, "NoneOrOff")
      ETL_ENUM_TYPE(ModeC, "ModeC")
      ETL_ENUM_TYPE(ModeS, "ModeS")
      ETL_ENUM_TYPE(Reserved, "Reserved")
      ETL_END_ENUM_TYPE
    };

    struct EConspicuityBits
    {
      enum enum_type : uint8_t
      {
        EC_NONE = 0,
        EC_FLARM = 1 << 0,
        EC_PILOTAWARE = 1 << 1,
        EC_FANET = 1 << 2,
        EC_OGN = 1 << 3,
        EC_ADSB_1090 = 1 << 4,
        EC_ADSB_978 = 1 << 5,
        EC_MODES_PCAS = 1 << 6,
        EC_RESERVED = 1 << 7,
      };

      ETL_DECLARE_ENUM_TYPE(EConspicuityBits, uint8_t)
      ETL_ENUM_TYPE(EC_NONE, "EC_NONE")
      ETL_ENUM_TYPE(EC_FLARM, "EC_FLARM")
      ETL_ENUM_TYPE(EC_PILOTAWARE, "EC_PILOTAWARE")
      ETL_ENUM_TYPE(EC_FANET, "EC_FANET")
      ETL_ENUM_TYPE(EC_OGN, "EC_OGN")
      ETL_ENUM_TYPE(EC_ADSB_1090, "EC_ADSB_1090")
      ETL_ENUM_TYPE(EC_ADSB_978, "EC_ADSB_978")
      ETL_ENUM_TYPE(EC_MODES_PCAS, "EC_MODES_PCAS")
      ETL_ENUM_TYPE(EC_RESERVED, "EC_RESERVED")
      ETL_END_ENUM_TYPE
    };

  private:
    ReceiveCapability mBandReceiveCapability_ = ReceiveCapability::None;
    ReceiveCapability oBandReceiveCapability_ = ReceiveCapability::None;
    ReceiveCapability oBandHdrReceiveCapability_ = ReceiveCapability::None;
    MobileState mobileState_ = MobileState::None;
    XpdrCapability xpdrCapability_ = XpdrCapability::NoneOrOff;
    EConspicuityBits eReceiveConspicuityBits_ = EConspicuityBits::EC_NONE;
    EConspicuityBits eTransmitConspicuityBits_ = EConspicuityBits::EC_NONE;
    bool adslTrafficUplinkClient_ = false;
    bool adslFisBUplinkCLient_ = false;
    NetworkPayload::ProtocolVersion maxProtocolversion_ = NetworkPayload::ProtocolVersion::LATEST;

  public:
    explicit StatusPayload() = default;

    Header::PayloadTypeIdentifier type() const
    {
      return Header::PayloadTypeIdentifier::STATUS;
    }

    MobileState mobileState() const { return mobileState_; }
    void mobileState(MobileState mobileState) { mobileState_ = mobileState; }

    XpdrCapability xpdrCapability() const { return xpdrCapability_; }

    void xpdrCapability(XpdrCapability xpdrCapability)
    {
      xpdrCapability_ = xpdrCapability;
    }

    // Ensure eReceiveConspicuityBits_ and eTransmitConspicuityBits_ can be set
    // bitwise OR
    void eReceiveConspicuityBits(EConspicuityBits eReceiveConspicuityBits)
    {
      eReceiveConspicuityBits_ = static_cast<EConspicuityBits>(static_cast<uint8_t>(eReceiveConspicuityBits_) | static_cast<uint8_t>(eReceiveConspicuityBits));
    }

    void eTransmitConspicuityBits(EConspicuityBits eTransmitConspicuityBits)
    {
      eTransmitConspicuityBits_ = static_cast<EConspicuityBits>(static_cast<uint8_t>(eTransmitConspicuityBits_) | static_cast<uint8_t>(eTransmitConspicuityBits));
    }

    EConspicuityBits eReceiveConspicuityBits() const
    {
      return eReceiveConspicuityBits_;
    }

    EConspicuityBits eTransmitConspicuityBits() const
    {
      return eTransmitConspicuityBits_;
    }

    void mBandReceiveCapability(ReceiveCapability rc)
    {
      mBandReceiveCapability_ = rc;
    }
    ReceiveCapability mBandReceiveCapability() const
    {
      return mBandReceiveCapability_;
    }

    void oBandReceiveCapability(ReceiveCapability rc)
    {
      oBandReceiveCapability_ = rc;
    }

    ReceiveCapability oBandReceiveCapability() const
    {
      return oBandReceiveCapability_;
    }

    void oBandHdrReceiveCapability(ReceiveCapability rc)
    {
      oBandHdrReceiveCapability_ = rc;
    }

    ReceiveCapability oBandHdrReceiveCapability() const
    {
      return oBandHdrReceiveCapability_;
    }

    // getters and setters for adslTrafficUplinkClient and adslFisBUplinkCLient
    bool adslTrafficUplinkClient() const
    {
      return adslTrafficUplinkClient_;
    }

    void adslTrafficUplinkClient(bool adslTrafficUplinkClient)
    {
      adslTrafficUplinkClient_ = adslTrafficUplinkClient;
    }

    bool adslFisBUplinkCLient() const
    {
      return adslFisBUplinkCLient_;
    }

    void adslFisBUplinkCLient(bool adslFisBUplinkCLient)
    {
      adslFisBUplinkCLient_ = adslFisBUplinkCLient;
    }

    NetworkPayload::ProtocolVersion maxProtocolversion() const
    {
      return maxProtocolversion_;
    }
    void maxProtocolversion(NetworkPayload::ProtocolVersion pv)
    {
      maxProtocolversion_ = pv;
    }

    /**
     * @brief Serialize the tracking payload to a bit stream.
     * @param writer The bit stream writer.
     */
    virtual size_t serialize_issue2(etl::bit_stream_writer &writer) const
    {
      writer.write_unchecked(static_cast<uint8_t>(maxProtocolversion_), 4U);
      writer.write_unchecked(static_cast<uint8_t>(mBandReceiveCapability_), 2U);
      writer.write_unchecked(static_cast<uint8_t>(oBandReceiveCapability_), 2U);
      writer.write_unchecked(static_cast<uint8_t>(oBandHdrReceiveCapability_), 2U);
      writer.write_unchecked(static_cast<uint8_t>(mobileState_), 2U);
      writer.write_unchecked(static_cast<uint8_t>(eReceiveConspicuityBits_), 8U);
      writer.write_unchecked(static_cast<uint8_t>(eTransmitConspicuityBits_), 8U);
      writer.write_unchecked(static_cast<uint8_t>(xpdrCapability_), 2U);
      writer.write_unchecked(adslTrafficUplinkClient_);
      writer.write_unchecked(adslFisBUplinkCLient_);
      writer.write_unchecked(0, 24U); // Reserved
      return 7;
    }

    /**
     * @brief Deserialize the tracking payload from a bit stream.
     * @param reader The bit stream reader.
     * @return The deserialized tracking payload.
     */
    static const StatusPayload deserialize_issue2(etl::bit_stream_reader &reader)
    {
      StatusPayload status;
      status.maxProtocolversion_ = static_cast<NetworkPayload::ProtocolVersion>(reader.read_unchecked<uint8_t>(4U));
      status.mBandReceiveCapability_ = ReceiveCapability(reader.read_unchecked<uint8_t>(2U));
      status.oBandReceiveCapability_ = ReceiveCapability(reader.read_unchecked<uint8_t>(2U));
      status.oBandHdrReceiveCapability_ = ReceiveCapability(reader.read_unchecked<uint8_t>(2U));
      status.mobileState_ = MobileState(reader.read_unchecked<uint8_t>(2U));
      status.eReceiveConspicuityBits_ = EConspicuityBits(reader.read_unchecked<uint8_t>(8U));
      status.eTransmitConspicuityBits_ = EConspicuityBits(reader.read_unchecked<uint8_t>(8U));
      status.xpdrCapability_ = XpdrCapability(reader.read_unchecked<uint8_t>(2U));
      status.adslTrafficUplinkClient_ = reader.read_unchecked<bool>();
      status.adslFisBUplinkCLient_ = reader.read_unchecked<bool>();
      reader.read_unchecked<uint8_t>(24U); // Reserved
      return status;
    }
  };
}; // namespace ADSL
