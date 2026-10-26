#pragma once

#include "constants.hpp"
#include "basemodule.hpp"
#include "models.hpp"
#include "ownshipstate.hpp"
#include "deviceactions.hpp"
#include "poolallocator.hpp"
#include "constants.hpp"

#include "etl/message.h"
#include "etl/message_router.h"
#include "etl/message_bus.h"
#include "etl/string.h"
#include "etl/set.h"
#include "etl/vector.h"
#include "etl/algorithm.h"

namespace GATAS
{

    /**
     * Send by ADSB Modules contains RAW ADSB message in the form of
     * *a8000fb18b51293820bcd5d0fe9c; in binary from
     */
    struct ADSBMessageBinMsg : public etl::message<1>
    {
        static constexpr uint8_t MAX_BINARY_LENGTH = 14;
        uint8_t length;
        uint8_t data[MAX_BINARY_LENGTH];
    };

    /**
     * GPS Message, received from an attached GPS device
     */
    struct GPSSentenceMsg : public etl::message<3>
    {
        const NMEAString sentence; // Received NMEA sentence, without LR/LF
        GPSSentenceMsg(const NMEAString &sentence_) : sentence(sentence_) {}
    };

    /**
     * NMEA Compatible message of length 83 chars including null term
     * Send to attached devices
     */
    struct DataPortMsg : public etl::message<4>
    {
        const NMEAString sentence; // Received NMEA sentence
        DataPortMsg(const NMEAString &sentence_) : sentence(sentence_) {}
    };

    /**
     * Aircraft Position of other aircraft
     */
    struct IngressAircraftPositionMsg : public etl::message<5>
    {
        const GATAS::AircraftPositionInfo position;
        int16_t rssidBm; // Received signal strength indicator in dB
        IngressAircraftPositionMsg(const GATAS::AircraftPositionInfo &position_, int16_t rssidBm_) : position(position_), rssidBm(rssidBm_) {}
        IngressAircraftPositionMsg(const GATAS::AircraftPositionInfo &position_) : position(position_), rssidBm(INT16_MIN) {}
    };

    /**
     * Data structure for a vector of AircraftPositionInfo entering into the system to be received by the AircraftTracker
     */
    struct IngressAircraftPositionsMsg : public etl::message<33>
    {
        static constexpr size_t MAX_POSITIONS = 10;
        etl::vector<GATAS::AircraftPositionInfo, 10> positions;
        IngressAircraftPositionsMsg(const etl::ivector<GATAS::AircraftPositionInfo> &positions_)
            : positions(positions_.begin(), positions_.begin() + etl::min(positions_.size(), MAX_POSITIONS))
        {
            GATAS_ASSERT(positions_.size() <= MAX_POSITIONS, "IngressAircraftPositionsMsg overflow");
        }
    };

    /**
     * Aircraft Position of other aircraft from the tracker
     */
    struct EgressAircraftPositionMsg : public etl::message<23>
    {
        const AircraftPositionInfo position;
        EgressAircraftPositionMsg(const AircraftPositionInfo &position_) : position(position_) {}
    };

    /**
     * Aircraft Position of other aircraft from the tracker
     */
    struct EgressAircraftPositionsMsg : public etl::message<35>
    {
        const AdslObandUplinkAircraft positions;
        const GATAS::RadioParameters radioParameters;
        uint8_t radioNo;
        EgressAircraftPositionsMsg(const AdslObandUplinkAircraft &positions_, const GATAS::RadioParameters &radioParameters_, uint8_t radioNo_)
            : positions(positions_), radioParameters(radioParameters_), radioNo(radioNo_) {}
    };

    struct TrackerStatsMsg : public etl::message<38>
    {
        uint8_t noTrackedAircraft;
        TrackerStatsMsg(uint8_t noTrackedAircraft_) : noTrackedAircraft(noTrackedAircraft_) {}
    };

    /**
     * Aircraft Position of our ownship
     */
    struct OwnshipPositionMsg : public etl::message<6>
    {
    };

    struct UtcTimeMsg : public etl::message<12>
    {
        int16_t year;        // Set with full year, e.g. 2021
        int8_t month;        // 1..12
        int8_t day;          // 1..31
        int8_t hour;         // 0..23
        int8_t minute;       // 0..59
        int8_t second;       // 0..59
        int16_t millisecond; // 0..999
        // Constructor
        UtcTimeMsg(int16_t year_, int8_t month_, int8_t day_, int8_t hour_, int8_t minute_, int8_t second_, int16_t millisecond_) : year(year_), month(month_), day(day_), hour(hour_), minute(minute_), second(second_), millisecond(millisecond_) {};
    };

    struct GpsStatsMsg : public etl::message<14>
    {
        GATAS::GpsStats gpsStats;
        GpsStatsMsg(const GATAS::GpsStats &gpsStats_) : gpsStats(gpsStats_) {}
    };

    struct BarometricPressureMsg : public etl::message<15>
    {
    };

    struct RadioRxMsgBase
    {
        mutable PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> frame;
        mutable size_t lengthBytes; // hack to allow reset the length on a const object from the messagebus
        uint32_t epochSeconds;
        uint32_t frequency;
        GATAS::DataSource dataSource;
        int8_t rssidBm;

        RadioRxMsgBase(GATAS::GlobalPoolConfiguration &pool, uint8_t *frame_, size_t lengthBytes_, uint32_t epochSeconds_, uint32_t frequency_, GATAS::DataSource dataSource_, int8_t rssidBm_)
            : frame(pool, frame_), lengthBytes(lengthBytes_), epochSeconds(epochSeconds_), frequency(frequency_), dataSource(dataSource_), rssidBm(rssidBm_) {}

        RadioRxMsgBase(PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> &&frame_, size_t lengthBytes_, uint32_t epochSeconds_, uint32_t frequency_, GATAS::DataSource dataSource_, int8_t rssidBm_)
            : frame(etl::move(frame_)), lengthBytes(lengthBytes_), epochSeconds(epochSeconds_), frequency(frequency_), dataSource(dataSource_), rssidBm(rssidBm_) {}

        uint32_t *frame32() const
        {
            return reinterpret_cast<uint32_t *>(frame.get());
        }

        etl::span<uint32_t> frame32Span() const
        {
            return etl::span<uint32_t>(reinterpret_cast<uint32_t *>(frame.get()), (lengthBytes + 3) / 4);
        }

        etl::span<uint8_t> frameSpan() const
        {
            return etl::span<uint8_t>(frame.get(), lengthBytes);
        }
    };

    struct RadioRxMsg : public RadioRxMsgBase, public etl::message<200>
    {
        explicit RadioRxMsg(GATAS::GlobalPoolConfiguration &pool, uint8_t *data_, size_t length_, uint32_t epochSeconds_, uint32_t frequency_, GATAS::DataSource dataSource_, int8_t rssidBm_)
            : RadioRxMsgBase(pool, data_, length_, epochSeconds_, frequency_, dataSource_, rssidBm_)
        {
        }

        explicit RadioRxMsg(PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> &&data_, size_t length_, uint32_t epochSeconds_, uint32_t frequency_, GATAS::DataSource dataSource_, int8_t rssidBm_)
            : RadioRxMsgBase(etl::move(data_), length_, epochSeconds_, frequency_, dataSource_, rssidBm_)
        {
        }
    };

    struct RadioRxManchesterMsg : public RadioRxMsgBase, public etl::message<201>
    {
        mutable PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> error;

        explicit RadioRxManchesterMsg(GATAS::GlobalPoolConfiguration &pool, uint8_t *data_, uint8_t *error_, size_t length_, uint32_t epochSeconds_, uint32_t frequency_, GATAS::DataSource dataSource_, int8_t rssidBm_)
            : RadioRxMsgBase(pool, data_, length_, epochSeconds_, frequency_, dataSource_, rssidBm_), error(pool, error_) {}

        explicit RadioRxManchesterMsg(PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> &&data_, GATAS::GlobalPoolConfiguration &pool, uint8_t *error_, size_t length_, uint32_t epochSeconds_, uint32_t frequency_, GATAS::DataSource dataSource_, int8_t rssidBm_)
            : RadioRxMsgBase(etl::move(data_), length_, epochSeconds_, frequency_, dataSource_, rssidBm_), error(pool, error_) {}

        uint32_t *err32()
        {
            return reinterpret_cast<uint32_t *>(error.get());
        }

        etl::span<uint32_t> error32Span() const
        {
            return etl::span<uint32_t>(reinterpret_cast<uint32_t *>(error.get()), (lengthBytes + 3) / 4);
        }

        etl::span<uint8_t> errorSpan() const
        {
            return etl::span<uint8_t>(error.get(), lengthBytes);
        }
    };

    /**
     * @brief Message to instruct a protocol to transmit the current data over the protocol
     *
     */
    struct RadioTxPositionRequestMsg : public etl::message<2>
    {
        const GATAS::RadioParameters radioParameters;
        uint8_t radioNo;
        RadioTxPositionRequestMsg(const GATAS::RadioParameters &radioParameters_, uint8_t radioNo_) : radioParameters(radioParameters_), radioNo(radioNo_) {};
    };

    /**
     * @brief Message send to transmit a frame over the radio
     *
     */
    struct RadioTxFrameMsg : public etl::message<202>
    {
        GATAS::RadioParameters radioParameters;
        mutable PoolOwnedPtr<GATAS::GlobalPoolConfiguration, const uint8_t> frame;
        size_t length;
        uint8_t radioNo;

        RadioTxFrameMsg(GATAS::GlobalPoolConfiguration &pool, const GATAS::RadioParameters &radioParameters_, const uint8_t *frame_, size_t length_, uint8_t radioNo_) : radioParameters(radioParameters_), frame(pool, frame_), length(length_), radioNo(radioNo_) {}
    };

    /**
     * @brief Message to control the radio to a new protocol, frequency etc
     *
     */
    struct RadioControlMsg : public etl::message<28>
    {
        const GATAS::RadioParameters radioParameters;
        uint8_t radioNo;
        RadioControlMsg(const GATAS::RadioParameters &radioParameters_, uint8_t radioNo_) : radioParameters(radioParameters_), radioNo(radioNo_) {};
    };

    struct ConfigUpdatedMsg : public etl::message<20>
    {
        const Configuration &config;
        const GATAS::Modulename moduleName;
        ConfigUpdatedMsg(const Configuration &config_, const GATAS::Modulename &moduleName_) : config(config_), moduleName(moduleName_) {};
        ConfigUpdatedMsg(const Configuration &config_, const etl::string_view &moduleName_) : config(config_), moduleName(moduleName_) {};
    };

    /**
     * Send to inform receivers of the current connected clients over TCP.
     */
    struct AccessPointClientsMsg : public etl::message<21>
    {
        using clientSet = etl::set<uint32_t, GATAS_MAXIMUM_TCP_CLIENTS>;
        const clientSet msg;
        AccessPointClientsMsg(const clientSet &msg_) : msg(msg_) {};
    };

    /**
     * Send to inform receivers of the current connected clients over TCP.
     */
    struct GdlMsg : public etl::message<22>
    {
        GDLData msg;
    };

    /**
     * Message send when WIFI connection state changes
     * NOTE: Don't change message ID!
     */
    struct WifiConnectionStateMsg : public etl::message<24>
    {
        GATAS::WifiMode wifiMode;
        uint32_t gatasIp;
        uint32_t gateWay;
        WifiConnectionStateMsg(GATAS::WifiMode wifiMode_) : wifiMode(wifiMode_), gatasIp(0), gateWay(0) {};
        WifiConnectionStateMsg(GATAS::WifiMode wifiMode_, uint32_t gatasIp_, uint32_t gateWay_) : wifiMode(wifiMode_), gatasIp(gatasIp_), gateWay(gateWay_) {};
    };

    /**
     * Message send of the current adaptive radius size.
     */
    struct AdapativeRadiusMsg : public etl::message<26>
    {
        uint32_t radius;
        AdapativeRadiusMsg(uint32_t radius_) : radius(radius_) {};
    };

    /**
     * Request WifiService to move into a specific WiFi mode.
     */
    struct WifiModeRequestMsg : public etl::message<27>
    {
        GATAS::WifiMode wifiMode;
        WifiModeRequestMsg(GATAS::WifiMode wifiMode_) : wifiMode(wifiMode_) {}
    };

    /**
     * Idle Message send at intervals that allows to due small tasks without creating a new task
     * Modules using this message should never block a task
     */
    struct IdleMsg : public etl::message<25>
    {
    };
    struct Every1SecMsg : public etl::message<29>
    {
    };
    struct Every5SecMsg : public etl::message<30>
    {
    };
    struct Every15SecMsg : public etl::message<31>
    {
    };
    struct Every30SecMsg : public etl::message<32>
    {
    };
    struct Every300SecMsg : public etl::message<34>
    {
    };

    /**
     * cobsMessage must be a complet emessage
     */
    struct GatasConnectTx : public etl::message<36>
    {
        mutable PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> cobsMessage;
        size_t length;
        GATAS::GatasConnectOutput output;

        GatasConnectTx(GATAS::GlobalPoolConfiguration &pool, GATAS::GatasConnectOutput output_, uint8_t *cobsMessage_, size_t length_)
            : cobsMessage(pool, cobsMessage_), length(length_), output(output_)
        {
            GATAS_ASSERT((length != 0 && cobsMessage.get() != nullptr), "Invalid Connect Msg");
        }

        GatasConnectTx(PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> &&cobsMessage_, size_t length_, GATAS::GatasConnectOutput output_)
            : cobsMessage(etl::move(cobsMessage_)), length(length_), output(output_)
        {
            GATAS_ASSERT((length != 0 && cobsMessage.get() != nullptr), "Invalid Connect Msg");
        }
    };

    /**
     * cobsMessage must be a complet emessage
     */
    struct GatasConnectRx : public etl::message<37>
    {
        mutable PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> cobsMessage;
        size_t length;
        GATAS::GatasConnectTransport source;

        GatasConnectRx(GATAS::GlobalPoolConfiguration &pool, GATAS::GatasConnectTransport source_, uint8_t *cobsMessage_, size_t length_)
            : cobsMessage(pool, cobsMessage_), length(length_), source(source_) {}

        GatasConnectRx(PoolOwnedPtr<GATAS::GlobalPoolConfiguration, uint8_t> &&cobsMessage_, size_t length_, GATAS::GatasConnectTransport source_)
            : cobsMessage(etl::move(cobsMessage_)), length(length_), source(source_) {}
    };
    struct DeviceActionRequestMsg : public etl::message<39>
    {
        DeviceAction action;
        explicit DeviceActionRequestMsg(DeviceAction action_) : action(action_) {}
    };

    struct SelectAircraftRequestMsg : public etl::message<42>
    {
        uint32_t icaoAddress;
        explicit SelectAircraftRequestMsg(uint32_t address) : icaoAddress(address) {}
    };

    // Requests a platform reset from Idle.
    struct SystemActionMsg : public etl::message<43>
    {
        DeviceAction action;
        explicit SystemActionMsg(DeviceAction action_) : action(action_) {}
    };
}
