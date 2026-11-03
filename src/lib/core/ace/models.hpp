#pragma once

#include "constants.hpp"
#include "poolallocator.hpp"

#include "etl/vector.h"
#include "etl/enum_type.h"

namespace CoreUtils
{
    // TODO: Reorganise some of our models, this one is neede,d but we cannot include coreutils
    struct distanceRelNorthRelEastInt;
}

namespace GATAS
{
    static constexpr int32_t INVALID_BARO_ALTITUDE = INT32_MIN;
    static constexpr float INVALID_QNH = -1.0f;
    static constexpr float INVALID_PRESSSURE_HPA = -1.0f;

    /*
     * Result after the POST_CONSTRUCT call to indicate the status of the module, eg what happened and if the module is in a good state to run
     */
    enum class PostConstruct : uint8_t
    {
        NA = 0,                       // No data yet avaiulable, use in the web modules
        OK = 1,                       // Module is ready to be started
        FAILED = 2,                   // Generale failure
        MEMORY = 3,                   // Memory allocation issues
        DEP_NOT_FOUND = 4,            // A dependency was needed but not loaded
        XQUEUE_ERROR = 5,             // FreeRTOS X queue error
        TASK_ERROR = 6,               // task creation failure
        HARDWARE_NOT_FOUND = 7,       // Hardware  not found
        HARDWARE_ERROR = 8,           // Hardware  found but not working correctly
        NETWORK_ERROR = 9,            // Netowrk issue
        CONFIG_ERROR = 10,            // Configuration issue (not found incorrect etc...)
        TIMER_ERROR = 11,             // task creation failure
        MUTEX_ERROR = 12,             // FreeRTOS X queue error
        HARDWARE_NOT_CONFIGURED = 13, // Indicate there was no configuration found
        SPINLOCK_ERROR = 14,          // No spinlock available
    };

    enum class PinType : uint8_t
    {
        UNKNOWN,
        SPI,
        CLK,
        MOSI,
        MISO,
        RST,
        TX,
        RX,
        BUSY,
        CS,
        O0,
        I0,
        DIO1,
        P0,
        P1,
        P2,
        AD0,
        DEV
    };

    // PinType alias to uint8_t
    PinType stringToPinType(const char *str);

    // Assigments follows closly GDL90 specification
    struct AircraftCategory
    {
        enum enum_type : uint8_t
        {
            UNKNOWN = 0,
            LIGHT = 1,       // < 15.500lbs
            SMALL = 2,       //   15.500lbs to 75.000lbs
            LARGE = 3,       // < 75.000lbs to 300.000lbs
            HIGH_VORTEX = 4, // Such as Boing 757
            HEAVY_ICAO = 5,  // > 300.000lbs
            AEROBATIC = 6,
            ROTORCRAFT = 7,
            // UnAssigned
            GLIDER = 9,
            LIGHT_THAN_AIR = 10,
            SKY_DIVER = 11,
            ULTRA_LIGHT_FIXED_WING = 12, // < 1500 lbs Fixed Wing only
            // UnAssigned
            UN_MANNED = 14,
            SPACE_VEHICLE = 15,
            // UnAssigned
            SURFACE_EMERGENCY_VEHICLE = 17,
            SURFACE_VEHICLE = 18, // SurfaceVehicle
            POINT_OBSTACLE = 19,
            CLUSTER_OBSTACLE = 20,
            LINE_OBSTACLE = 21,
            // .. reserved to 39
            // From below are custom values
            GYROCOPTER = 40,
            HANG_GLIDER = 41,
            PARA_GLIDER = 42,
            DROP_PLANE = 43,
            MILITARY = 44,
        };

        ETL_DECLARE_ENUM_TYPE(AircraftCategory, uint8_t)
        ETL_ENUM_TYPE(UNKNOWN, "Unknown")
        ETL_ENUM_TYPE(LIGHT, "Light")
        ETL_ENUM_TYPE(SMALL, "Small")
        ETL_ENUM_TYPE(LARGE, "Large")
        ETL_ENUM_TYPE(HIGH_VORTEX, "High Vortex")
        ETL_ENUM_TYPE(HEAVY_ICAO, "Heavy (ICAO)")
        ETL_ENUM_TYPE(AEROBATIC, "Aerobatic")
        ETL_ENUM_TYPE(ROTORCRAFT, "Helicopter")
        // Unassigned
        ETL_ENUM_TYPE(GLIDER, "Glider")
        ETL_ENUM_TYPE(LIGHT_THAN_AIR, "Balloon")
        ETL_ENUM_TYPE(SKY_DIVER, "Sky Diver")
        ETL_ENUM_TYPE(ULTRA_LIGHT_FIXED_WING, "Ultra Light")
        // Unassigned
        ETL_ENUM_TYPE(UN_MANNED, "Unmanned aerial vehicle")
        ETL_ENUM_TYPE(SPACE_VEHICLE, "Space Vehicle")
        // Unassigned
        ETL_ENUM_TYPE(SURFACE_EMERGENCY_VEHICLE, "Surface Emergency Vehicle")
        ETL_ENUM_TYPE(SURFACE_VEHICLE, "Surface Vehicle")
        ETL_ENUM_TYPE(POINT_OBSTACLE, "Point Obstacle")
        ETL_ENUM_TYPE(CLUSTER_OBSTACLE, "Cluster Obstacle")
        ETL_ENUM_TYPE(LINE_OBSTACLE, "Line Obstacle")
        // From below are custom values
        ETL_ENUM_TYPE(GYROCOPTER, "Gyrocopter")
        ETL_ENUM_TYPE(HANG_GLIDER, "Hang Glider")
        ETL_ENUM_TYPE(PARA_GLIDER, "Para Glider")
        ETL_ENUM_TYPE(DROP_PLANE, "Drop Plane")
        ETL_ENUM_TYPE(MILITARY, "Military")
        ETL_END_ENUM_TYPE
    };

    template <typename EnumType, typename MappingType>
    struct Mapping
    {
        EnumType type;
        MappingType name;
    };

    template <typename EnumType, typename MappingType, size_t Size>
    MappingType enumToString(const Mapping<EnumType, MappingType> (&mapping)[Size], EnumType enumValue, MappingType defaultValue)
    {
        for (const auto &entry : mapping)
        {
            if (entry.type == enumValue)
            {
                return entry.name;
            }
        }
        return defaultValue;
    }
    template <typename EnumType, typename MappingType, size_t Size>
    EnumType stringToEnum(const Mapping<EnumType, MappingType> (&mapping)[Size], const char *str, EnumType defaultValue)
    {
        if (!str)
        {
            return defaultValue;
        }
        for (const auto &entry : mapping)
        {
            if (strcasecmp(entry.name, str) == 0)
            {
                return entry.type;
            }
        }
        return defaultValue; // Return an invalid value if no match is found
    }

    // Aircrasft type to string
    const char *aircraftTypeToString(GATAS::AircraftCategory at);

    // String to AircraftCategory
    AircraftCategory stringToAircraftCategory(const char *str);

    /**
     * @brief The AddressType enum indicates how the aircraft can be looked up and identified
     * it does not indicate over what channel the aircraft is transmitting (OGN. Flarm etc..)
     * Flarm can send a address type of 0x2 to indicate FLARM address or 0x1 as ICAO address
     * 0x00 is used as RANDOM
     */
    enum class AddressType : uint8_t
    {
        RANDOM = 0, // random ID, configured or if stealth mode is activated either on the target or own aircraft
        ICAO = 1,   // official ICAO 24-bit aircraft address
        FLARM = 2,  // fixed FLARM ID (chosen by FLARM)
        OGN = 3,    // fixed OGN ID (chosen by OGN)
        FANET = 4,  // FANET comes in like this via ADS-L
        ADSL = 5,
        RESERVED = 6, // RESERVED comes in like this via ADS-L
        UNKNOWN = 7,  // UNKNOWN
    };

    // Get a string representation of a AddressType
    const char *addressTypeToString(GATAS::AddressType ds);

    // Maps a string into a AddressType
    GATAS::AddressType stringToAddressType(const char *str);

    /**
     * Datasource is the internal service that provided and knows how to handle the data
     */
    enum class DataSource : uint8_t
    {
        FLARM = 0,
        _TRANSPROTOCOLSSTART = 0,
        ADSLM = 1,
        ADSLO_HDR = 2,
        FANET = 3,
        OGN = 4,
        _RADIO = 5,          // ANything beflore Radio can only be received over hardware
        _TRANSPROTOCOLS = 5, // Indicate maximum RADIO that can be received over low power (868MHZ etc..) used to limit array sizes
        ADSB = 5,
        MLAT = 6,
        ADSLFLARM = 253, // Combination of ADSL/FLARM, not an acutal protocol but needed for RX of multiple protocols
        ADSLOGN = 254,   // Combination of ADSL/OGN, not an acutal protocol but needed for RX of multiple protocols
        NONE = 255       // Note: Never use this! Unly used for stringToEnum(..)
    };

    // Get a string representation of a datasource
    const char *dataSourceIntToString(uint8_t ds);
    const char *toString(DataSource ds);
    const char *toShortString(DataSource ds);
    DataSource stringToDataSource(const char *str);

    struct DataSourceMode
    {
        enum enum_type : uint8_t
        {
            RX = 1,
            TX = 2,
            RX_TX = 3
        };

        ETL_DECLARE_ENUM_TYPE(DataSourceMode, uint8_t)
        ETL_ENUM_TYPE(RX, "RX")
        ETL_ENUM_TYPE(TX, "TX")
        ETL_ENUM_TYPE(RX_TX, "RX_TX")
        ETL_END_ENUM_TYPE
    };

    DataSourceMode stringToDataSourceMode(const char *str);

    struct DataSourceConfig
    {
        DataSource dataSource;
        DataSourceMode mode;

        bool isRx() const
        {
            return (mode == DataSourceMode::RX || mode == DataSourceMode::RX_TX);
        }

        bool isTx() const
        {
            return (mode == DataSourceMode::TX || mode == DataSourceMode::RX_TX);
        }
    };

    namespace Config
    {
        struct Conspicuity
        {
            AircraftAddress icaoAddress;
            AircraftCategory category;
            AddressType addressType;
            bool stealth;
            bool noTrack;
            bool groundStation;
            int16_t heightAboveGps = 0;
        };

        /**
         * Object that specifies how to configure GATAS for this aircraft
         */
        struct GaTasConfiguration
        {
            Conspicuity conspicuity;
            etl::vector<GATAS::DataSourceConfig, static_cast<uint8_t>(GATAS::DataSource::_TRANSPROTOCOLS)> protocols;
            etl::vector<uint32_t, GATAS::MAX_AIRCRAFT_CONFIG> allIcaoAddresses; // List of all configured hex codes of all aircraft
        };

        /**
         * SSDPassword structure
         */
        struct WifiNamePassword
        {
            GATAS::SsidOrPasswdStr ssid;
            GATAS::SsidOrPasswdStr password;
            WifiNamePassword() : ssid(""), password("") {}
            WifiNamePassword(const etl::istring &ssid, const etl::istring &password)
                : ssid(ssid), password(password)
            {
            }
            WifiNamePassword(const char *ssid, const char *password)
                : ssid(ssid), password(password)
            {
            }
        };

        /**
         * WifiService data
         */
        struct WifiServiceData
        {
            WifiNamePassword ap;
            // When true, the accesspoint is disable and only Client module is enabled
            bool apDisabled;
            etl::vector<WifiNamePassword, 4> clients;
        };

        /**
         * IP Adress Port Structure
         */
        struct IpPort
        {
            // For now IPv4 (may be add LwIp to case and add ip_addr_t?? does not make it very portable..)
            uint32_t ip;
            uint16_t port;
        };
    };

    struct WifiMode
    {
        enum enum_type : uint8_t
        {
            NC = 0,
            AP = 1,
            CLIENT = 2,
        };

        ETL_DECLARE_ENUM_TYPE(WifiMode, uint8_t)
        ETL_ENUM_TYPE(NC, "No Connection")
        ETL_ENUM_TYPE(AP, "Access Point Mode")
        ETL_ENUM_TYPE(CLIENT, "Client Mode")
        ETL_END_ENUM_TYPE
    };

    // struct OwnshipMinimalPositionInfo
    // {
    //     AircraftAddress icaoAddress = 0;
    //     float lat = 0;
    //     float lon = 0;
    //     int32_t ellipseHeight = 0;
    // };

    struct OwnshipPositionInfo
    {
        uint32_t timestamp; // Timestamp when the position was received
        float lat;
        float lon;
        int32_t ellipseHeight;                   // Height above the Ellipsoid (WGS84) in meters. For aircraft where altitude is based from BARO, this is an estimate
        float verticalSpeed;                     // in m/s
        float groundSpeed;                       // in m/s
        float track;                             // 0..359
        float hTurnRate;                         // deg/s Turn rate in the horizontal plane
                                                 //        float velocityNorth;             // North velocity in m/s
                                                 //        float velocityEast;              // East velocity in m/s
        int16_t geoidSeparation;                 // The distance from the surface of an ellipsoid to the surface of the geoid.
        bool airborne;                           // Is the aircraft airborne, can this be taken from GS? It can be rare under normal situations that GS is low, even though we are flying (large headwind??)
        Config::Conspicuity conspicuity;         // Configuration for this aircraft, used to send out the correct data
        int32_t baroAlt = INVALID_BARO_ALTITUDE; // Pressure altitude in meters, when available
        int32_t heightMsl() const
        {
            return ellipseHeight - geoidSeparation;
        }

        // const OwnshipMinimalPositionInfo assignTo() const
        // {
        //     return OwnshipMinimalPositionInfo{conspicuity.icaoAddress, lat, lon, ellipseHeight};
        // }
    };

    /**
     * Aircraft location message and time of reception
     * Note: GPS use a theoretical sea level estimated by a World Geodetic System (WGS84)
     */
    struct AircraftPositionInfo
    {
        uint32_t timestamp;
        GATAS::CallSign callSign;
        int16_t squawk;

        AircraftAddress address;
        AddressType addressType;
        DataSource dataSource;
        AircraftCategory aircraftType;
        bool stealth;  // Privacy/Stealth option
        bool noTrack;  // this aircraft does not want to be tracked
        bool airborne; // Is the aircraft airborne
        float lat;
        float lon;
        int32_t ellipseHeight; // Altitude above the GeoId (MSL) in meters. For aircraft where altitude is based from BARO, this is an estimate
        float verticalSpeed;   // in m/s
        float groundSpeed;     // in m/s
        int16_t track;         // 0..359
        float hTurnRate;       // deg/s horizontal turn rate; 0 means straight flight, not unknown

        // These can be used by received to understand where the target is relative to ownship
        uint32_t distanceFromOwn; // Distance to ownship in meters,
        //        int16_t bearingFromOwn;   // Bearing to ownship in degrees, currently only used in AntennaRadiationPattern?
        int32_t pressureAlt = INVALID_BARO_ALTITUDE; // Pressure altitude in meters, when available

        CoreUtils::distanceRelNorthRelEastInt relativeFromOwn(float ownshipLat, float ownshipLon) const;
//        CoreUtils::distanceRelNorthRelEastInt relativeFromOwn(const OwnshipMinimalPositionInfo &ownship) const;
        CoreUtils::distanceRelNorthRelEastInt relativeFromOwn(const OwnshipPositionInfo &ownship) const;
        int32_t relNorthFromOwn(float ownshipLat, float ownshipLon) const;
//        int32_t relNorthFromOwn(const OwnshipMinimalPositionInfo &ownship) const;
        int32_t relNorthFromOwn(const OwnshipPositionInfo &ownship) const;
        int32_t relEastFromOwn(float ownshipLat, float ownshipLon) const;
//        int32_t relEastFromOwn(const OwnshipMinimalPositionInfo &ownship) const;
        int32_t relEastFromOwn(const OwnshipPositionInfo &ownship) const;

        AircraftPositionInfo(uint32_t timestamp_, GATAS::CallSign callSign_, AircraftAddress address_, AddressType addressType_, DataSource dataSource_, AircraftCategory aircraftType_, bool stealth_, bool noTrack_, bool airborne_, float lat_, float lon_, int32_t ellipseHeight_, float verticalSpeed_, float groundSpeed_, int16_t track_, float hTurnRate_, uint32_t distanceFromOwn_, int16_t squawk_ = -1, int32_t pressureAlt_ = INVALID_BARO_ALTITUDE /*, int16_t bearingFromOwn_*/)
            : timestamp(timestamp_), callSign(callSign_), squawk(squawk_), address(address_), addressType(addressType_), dataSource(dataSource_), aircraftType(aircraftType_), stealth(stealth_), noTrack(noTrack_), airborne(airborne_), lat(lat_), lon(lon_), ellipseHeight(ellipseHeight_), verticalSpeed(verticalSpeed_), groundSpeed(groundSpeed_), track(track_), hTurnRate(hTurnRate_), distanceFromOwn(distanceFromOwn_), pressureAlt(pressureAlt_) // , bearingFromOwn(bearingFromOwn_)
        {
        }
        // Default constructor
        AircraftPositionInfo() : timestamp(0), callSign(""), squawk(-1), address(0), addressType(AddressType::RANDOM), dataSource(DataSource::NONE), aircraftType(AircraftCategory::UNKNOWN), stealth(false), noTrack(false), airborne(false), lat(0), lon(0), ellipseHeight(0), verticalSpeed(0), groundSpeed(0), track(0), hTurnRate(0), distanceFromOwn(UINT32_MAX) // , bearingFromOwn(INT16_MIN)
        {
        }

        AircraftPositionInfo(AircraftAddress address_) : AircraftPositionInfo()
        {
            address = address_;
        }

        bool isPressureAltitudeValid() const 
        {
            return pressureAlt != INVALID_BARO_ALTITUDE;
        }
    };

    enum class Modulation : uint8_t
    {
        NONE = 0,
        GFSK,
        LORA,
    };

    const char *modulationToString(GATAS::Modulation mode);

    struct RfConfig
    {
        GATAS::Modulation mode;
        uint32_t baseFrequency;
        uint32_t channelSeperation;
        int8_t powerdBm;
        uint32_t channelBandwidth;
        uint32_t chipRate;
        uint16_t freqDiv;
        uint8_t gaussBt;
    };

    struct LinkLayerConfig
    {
        uint8_t pcId;                    // Internally used to opnise tranceiver state
        GATAS::DataSource __dataSource;  // Data source
        bool manchester;                 // True when data is manchester encoded
        uint8_t packetLength;            // Total packet length including CRC, but when left to zero. THe protocol send the packet length
        uint8_t txPreambleLength;        // Preamble length in bits during transmission
        uint8_t syncLength;              // Length of the sync word in bytes
        uint8_t syncSkipInRxLength;      // When setting the receiver, skip n bytes from the sync
        etl::array<uint8_t, 8> syncWord; // Sync word for RX/TX depending on the mode we only take portion or full sync

        constexpr GATAS::DataSource dataSource() const
        {
            return __dataSource;
        }

        /**
         * Use this during Tx context of matching datasources
         */
        constexpr bool isTxDataSource(GATAS::DataSource ds) const
        {
            return __dataSource == ds;
        }

        /**
         * USe this during Rx context of matching datasources
         * 23 Jan 2026 : This was added because now RX Datasource can be combined ADSL/FLARM or ADSL/OGN
         */
        constexpr bool isRxDataSource(GATAS::DataSource ds) const
        {
            switch (__dataSource)
            {
            case GATAS::DataSource::ADSLFLARM:
                return ds == GATAS::DataSource::FLARM || ds == GATAS::DataSource::ADSLM;

            case GATAS::DataSource::ADSLOGN:
                return ds == GATAS::DataSource::OGN || ds == GATAS::DataSource::ADSLM;

            default:
                return __dataSource == ds;
            }
        }
    };

    struct RadioParameters
    {
        static constexpr GATAS::RfConfig DEFAULT{GATAS::Modulation::GFSK, 868'200'000, 200'000, 14, 234300, 100000, 50000, 5}; // 868.2 / 868.4

        const GATAS::LinkLayerConfig *config = nullptr;
        const GATAS::RfConfig *frequency = &DEFAULT;
        uint32_t hopFrequency = 0;
        uint8_t id;
        uint8_t codingRate = 0;

        constexpr RadioParameters(const GATAS::LinkLayerConfig *config_,
                                  const GATAS::RfConfig *frequency_,
                                  uint32_t hopFrequency_,
                                  uint8_t id_)
            : config(config_), frequency(frequency_), hopFrequency(hopFrequency_), id(id_), codingRate(0) {}

        constexpr RadioParameters(const GATAS::LinkLayerConfig *config_,
                                  const GATAS::RfConfig *frequency_,
                                  uint32_t hopFrequency_,
                                  uint8_t id_,
                                  uint8_t codingRate_)
            : config(config_), frequency(frequency_), hopFrequency(hopFrequency_), id(id_), codingRate(codingRate_) {}

        RadioParameters() : config(nullptr), frequency(nullptr), hopFrequency(0), id(0), codingRate(8) {}
    };

    /**
     * @brief Radio frame received by transceiver. Can contain both LORA and GFSK data frames
     *
     */
    struct DataFrame
    {
        uint32_t epochSeconds;
        uint32_t frequency;
        const GATAS::LinkLayerConfig *config;
        PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> frame;
        size_t length; // Length of the data frame in bytes
        int8_t rssidBm;
    };

    /**
     * Binry store is used to store occasionaly data that does not require user modification
     * Usually hardware status and performance. Used in teh configuration class to store the
     * unique GATAS ID and a magic to check if the flash needs to be reset
     * Other valueas could be added that could be of intereset during lifetime of the project
     */
    struct BinaryStore
    {
        static constexpr uint32_t MAGIC = 0xCAFEBABE;
        // Magic to test if the flash needs to be reset
        uint32_t magic = MAGIC;
        // Unique ID to track this device.
        // This is used to beable to show a website to the user uniqely without
        // other hijacking it's configuration. THis is not used to track the user.
        uint64_t gatasId;
    };

    // Values from https://en.wikipedia.org/wiki/Dilution_of_precision_(navigation)
    enum class pDopInterpretation : uint8_t
    {
        IDEAL = 0,     // .. 1
        EXCELLENT = 1, // .. 2
        GOOD = 2,      // ..5
        MODERATE = 5,  // ..10
        FAIR = 10,     // .. 20
        POOR = 21      // > 20
    };

    const char *DOPInterpretationToString(pDopInterpretation value);

    pDopInterpretation floatToDOPInterpretation(float dop);

    struct GpsFixType
    {
        enum enum_type : uint8_t
        {
            NO_FIX = 1,
            D2 = 2,
            D3 = 3,
            DGPS = 4,
        };

        ETL_DECLARE_ENUM_TYPE(GpsFixType, uint8_t)
        ETL_ENUM_TYPE(NO_FIX, "No Fix")
        ETL_ENUM_TYPE(D2, "2D")
        ETL_ENUM_TYPE(D3, "3D")
        ETL_ENUM_TYPE(DGPS, "DGPS")
        ETL_END_ENUM_TYPE
    };

    struct GpsFix
    {
        GpsFixType fixType;
        bool hasFix;
        GpsFix() : fixType(GpsFixType::NO_FIX), hasFix(false) {}
        GpsFix(GpsFixType fix_) : fixType(fix_), hasFix(fix_ == GpsFixType::D3 || fix_ == GpsFixType::DGPS) {}
    };

    struct GpsStats
    {
        GATAS::GpsFix gpsFix;       // gpsFix, calculated from GPS
        uint8_t satsUsedForFix = 0; // From GGA Sentence
        float pDop = 100.f;         // From GSA Sentence
        float hDop = 100.f;         // From GSA Sentence
        float vDop = 100.f;         // From GSA Sentence
    };

    // COBS ownship-pressure source values, matching the server.
    enum class PressureSource : uint8_t
    {
        Unavailable = 0,
        Calculated = 1,
        PressureSensor = 2,
        PRESSURE_SOURCE_NO_ITEMS = 3
    };

    struct BarometricPressure
    {
        float pressurehPa = GATAS::INVALID_PRESSSURE_HPA; // Ambient pressure in hPa; INVALID_PRESSSURE_HPA when unavailable
        PressureSource source = PressureSource::Unavailable;

        BarometricPressure(float pressurehPa_ = GATAS::INVALID_PRESSSURE_HPA, PressureSource source_ = PressureSource::Unavailable)
            : pressurehPa(pressurehPa_), source(source_)
        {
        }
    };

    /**
     * Transport destination for an outgoing GATAS Connect message.
     * A message may target UDP, Bluetooth, both transports, or neither.
     */
    struct GatasConnectOutput
    {
        enum enum_type : uint8_t
        {
            UDP = 0,
            Bluetooth = 1,
            UDPAndBluetooth = 2,
            NOOP = 255,
        };

        ETL_DECLARE_ENUM_TYPE(GatasConnectOutput, uint8_t)
        ETL_ENUM_TYPE(UDP, "UDP")
        ETL_ENUM_TYPE(Bluetooth, "BlueTooth")
        ETL_ENUM_TYPE(UDPAndBluetooth, "UDP + BlueTooth")
        ETL_ENUM_TYPE(NOOP, "Noop")
        ETL_END_ENUM_TYPE

    public:
        /** Return true when the message should be delivered by the UDP transport. */
        bool usesUDP() const
        {
            return value == UDP || value == UDPAndBluetooth;
        }

        /** Return true when the message should be delivered by the Bluetooth transport. */
        bool usesBluetooth() const
        {
            return value == Bluetooth || value == UDPAndBluetooth;
        }

        /**
         * Add Bluetooth to the destination without removing UDP.
         * Configuration announcements use this so Companion can discover the device.
         */
        GatasConnectOutput withBluetooth() const
        {
            return value == UDP ? GatasConnectOutput(UDPAndBluetooth) : *this;
        }

        /**
         * Prefer UDP for traffic requests when recent UDP traffic proves that path is active.
         * Only the combined destination is reduced; UDP-only and Bluetooth-only remain unchanged.
         */
        GatasConnectOutput preferUDP(bool udpTrafficActive) const
        {
            return value == UDPAndBluetooth && udpTrafficActive ? GatasConnectOutput(UDP) : *this;
        }
    };

    enum class GatasConnectTransport : uint8_t
    {
        UDP,
        Bluetooth,
    };

};
