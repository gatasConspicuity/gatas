#include "../models.hpp"
#include "../coreutils.hpp"

namespace GATAS
{

    struct PinMapping
    {
        const char *name;
        PinType type;
    };
    constexpr Mapping<PinType, const char *> pinMappings[] =
        {
            {PinType::CLK, "CLK"},
            {PinType::MOSI, "MOSI"},
            {PinType::MISO, "MISO"},
            {PinType::RST, "RST"},
            {PinType::TX, "TX"},
            {PinType::RX, "RX"},
            {PinType::BUSY, "BUSY"},
            {PinType::CS, "CS"},
            {PinType::O0, "O0"},
            {PinType::I0, "I0"},
            {PinType::DIO1, "DIO1"},
            {PinType::P0, "P0"},
            {PinType::P1, "P1"},
            {PinType::P2, "P2"},
            {PinType::AD0, "AD0"},
            {PinType::SPI, "SPI"},
            {PinType::DEV, "DEV"},
            {PinType::SDA, "SDA"},
            {PinType::SCL, "SCL"}};

    PinType stringToPinType(const char *str)
    {
        return stringToEnum(pinMappings, str, PinType::UNKNOWN);
    }

    constexpr Mapping<AddressType, const char *> addressMappings[] =
        {
            {AddressType::RANDOM, "RANDOM"},
            {AddressType::ICAO, "ICAO"},
            {AddressType::FLARM, "FLARM"},
            {AddressType::OGN, "OGN"},
            {AddressType::FANET, "FANET"},
            {AddressType::ADSL, "ADSL"},
            {AddressType::RESERVED, "RESERVED"},
            {AddressType::UNKNOWN, "UNKNOWN"}};

    const char *addressTypeToString(AddressType ds)
    {
        return enumToString(addressMappings, ds, "UNKNOWN");
    };

    AddressType stringToAddressType(const char *str)
    {
        return stringToEnum(addressMappings, str, AddressType::UNKNOWN);
    };

    constexpr Mapping<ConspicuityProtocol, const char *> protocolMappings[] =
        {
            {ConspicuityProtocol::FLARM, "Flarm"},
            {ConspicuityProtocol::ADSL_M_BAND, "ADSL"}, // Also used by the saved ADS-L setting.
            {ConspicuityProtocol::ADSL_O_BAND_HDR, "ADSL Hdr"},
            {ConspicuityProtocol::FANET, "Fanet"},
            {ConspicuityProtocol::ADSB, "ADSB"},
            {ConspicuityProtocol::MLAT, "MLAT"},
            {ConspicuityProtocol::OGN, "OGN"},
            {ConspicuityProtocol::NONE, "NONE"}};

    constexpr Mapping<ConspicuityProtocol, const char *> protocolShortMappings[] =
        {
            {ConspicuityProtocol::FLARM, "fl"},
            {ConspicuityProtocol::ADSL_M_BAND, "am"},
            {ConspicuityProtocol::ADSL_O_BAND_HDR, "ah"},
            {ConspicuityProtocol::FANET, "fa"},
            {ConspicuityProtocol::ADSB, "ab"},
            {ConspicuityProtocol::MLAT, "ml"},
            {ConspicuityProtocol::OGN, "og"},
            {ConspicuityProtocol::NONE, "un"}};

    const char *toString(ConspicuityProtocol ds)
    {
        return enumToString(protocolMappings, ds, "UNKNOWN");
    }
    const char *toShortString(ConspicuityProtocol ds)
    {
        return enumToString(protocolShortMappings, ds, "un");
    }
    ConspicuityProtocol stringToConspicuityProtocol(const char *str)
    {
        return stringToEnum(protocolMappings, str, ConspicuityProtocol::NONE);
    }

    struct pDopInterpretationMapping
    {
        pDopInterpretation type;
        const char *name;
    };

    constexpr Mapping<pDopInterpretation, const char *> interpretationMappings[] =
        {
            {pDopInterpretation::IDEAL, "Ideal"},
            {pDopInterpretation::EXCELLENT, "Excellent"},
            {pDopInterpretation::GOOD, "Good"},
            {pDopInterpretation::MODERATE, "Moderate"},
            {pDopInterpretation::FAIR, "Fair"},
            {pDopInterpretation::POOR, "Poor"}};

    const char *DOPInterpretationToString(pDopInterpretation interpretation)
    {
        return enumToString(interpretationMappings, interpretation, "Poor");
    }

    pDopInterpretation floatToDOPInterpretation(float dop)
    {
        if (dop < static_cast<uint8_t>(pDopInterpretation::EXCELLENT))
            return pDopInterpretation::IDEAL;
        if (dop < static_cast<uint8_t>(pDopInterpretation::GOOD))
            return pDopInterpretation::EXCELLENT;
        if (dop < static_cast<uint8_t>(pDopInterpretation::MODERATE))
            return pDopInterpretation::GOOD;
        if (dop < static_cast<uint8_t>(pDopInterpretation::FAIR))
            return pDopInterpretation::MODERATE;
        if (dop < static_cast<uint8_t>(pDopInterpretation::POOR))
            return pDopInterpretation::FAIR;
        return pDopInterpretation::POOR;
    }

    constexpr Mapping<Modulation, const char *> modulationMapping[] =
    {
        {Modulation::GFSK, "Gfsk"},
        {Modulation::LORA, "Lora"},
        {Modulation::OOK, "Ook"},
        {Modulation::NONE, "None"},
    };

    const char *modulationToString(GATAS::Modulation at)
    {
        return enumToString(modulationMapping, at, "-");
    }

    constexpr Mapping<ProtocolMode::enum_type, const char *> ProtocolModeMapping[] =
        {
            {ProtocolMode::TX, "TX"},
            {ProtocolMode::RX, "RX"},
            {ProtocolMode::RX_TX, "RX_TX"}
        };

    ProtocolMode stringToProtocolMode(const char *str)
    {
        return stringToEnum(ProtocolModeMapping, str, ProtocolMode::RX_TX);
    }

    CoreUtils::distanceRelNorthRelEastInt AircraftPositionInfo::relativeFromOwn(float ownshipLat, float ownshipLon) const
    {
        return CoreUtils::getDistanceRelNorthRelEastInt(ownshipLat, ownshipLon, lat, lon);
    }

    // CoreUtils::distanceRelNorthRelEastInt AircraftPositionInfo::relativeFromOwn(const OwnshipMinimalPositionInfo &ownship) const
    // {
    //     return relativeFromOwn(ownship.lat, ownship.lon);
    // }

    CoreUtils::distanceRelNorthRelEastInt AircraftPositionInfo::relativeFromOwn(const OwnshipPositionInfo &ownship) const
    {
        return relativeFromOwn(ownship.lat, ownship.lon);
    }

    int32_t AircraftPositionInfo::relNorthFromOwn(float ownshipLat, float ownshipLon) const
    {
        return relativeFromOwn(ownshipLat, ownshipLon).relNorth;
    }

    // int32_t AircraftPositionInfo::relNorthFromOwn(const OwnshipMinimalPositionInfo &ownship) const
    // {
    //     return relativeFromOwn(ownship).relNorth;
    // }

    int32_t AircraftPositionInfo::relNorthFromOwn(const OwnshipPositionInfo &ownship) const
    {
        return relativeFromOwn(ownship).relNorth;
    }

    int32_t AircraftPositionInfo::relEastFromOwn(float ownshipLat, float ownshipLon) const
    {
        return relativeFromOwn(ownshipLat, ownshipLon).relEast;
    }

    // int32_t AircraftPositionInfo::relEastFromOwn(const OwnshipMinimalPositionInfo &ownship) const
    // {
    //     return relativeFromOwn(ownship).relEast;
    // }

    int32_t AircraftPositionInfo::relEastFromOwn(const OwnshipPositionInfo &ownship) const
    {
        return relativeFromOwn(ownship).relEast;
    }
}
