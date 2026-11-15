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

    constexpr Mapping<DataSource, const char *> dataSourceMappings[] =
        {
            {DataSource::FLARM, "Flarm"},
            {DataSource::ADSLM, "ADSL"}, // <-- Don't change to ADSM, this is also used for the frontend selection
            {DataSource::ADSLO_HDR, "ADSL Hdr"},
            {DataSource::ADSLFLARM, "ADSL FLARM"},
            {DataSource::ADSLOGN, "ADSL OGN"},
            {DataSource::FANET, "Fanet"},
            {DataSource::ADSB, "ADSB"},
            {DataSource::MLAT, "MLAT"},
            {DataSource::OGN, "OGN"},
            {DataSource::NONE, "NONE"}};

    constexpr Mapping<DataSource, const char *> dataSourceShortMappings[] =
        {
            {DataSource::FLARM, "fl"},
            {DataSource::ADSLM, "am"},
            {DataSource::ADSLO_HDR, "ah"},
            {DataSource::FANET, "fa"},
            {DataSource::ADSB, "ab"},
            {DataSource::MLAT, "ml"},
            {DataSource::OGN, "og"},
            {DataSource::ADSLFLARM, "af"},
            {DataSource::ADSLOGN, "ao"},
            {DataSource::NONE, "un"}};

    const char *dataSourceIntToString(uint8_t ds)
    {
        return toString(static_cast<DataSource>(ds));
    }
    const char *toString(DataSource ds)
    {
        return enumToString(dataSourceMappings, ds, "UNKNOWN");
    }
    const char *toShortString(DataSource ds)
    {
        return enumToString(dataSourceShortMappings, ds, "un");
    }
    DataSource stringToDataSource(const char *str)
    {
        return stringToEnum(dataSourceMappings, str, DataSource::NONE);
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

    constexpr Mapping<DataSourceMode::enum_type, const char *> DataSourceModeMapping[] =
        {
            {DataSourceMode::TX, "TX"},
            {DataSourceMode::RX, "RX"},
            {DataSourceMode::RX_TX, "RX_TX"}
        };

    DataSourceMode stringToDataSourceMode(const char *str)
    {
        return stringToEnum(DataSourceModeMapping, str, DataSourceMode::RX_TX);
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
