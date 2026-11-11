#pragma once

/* System. */
#include <stdint.h>
#include <algorithm>

/* Vendor. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "message_buffer.h"

/* PICO. */
#include "pico/stdlib.h"
#include "pico/binary_info.h"

/* Vendor. */
#include "etl/string.h"
#include "etl/algorithm.h"

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/ownshipstate.hpp"
#include "ace/coreutils.hpp"
#include "ace/basemodule.hpp"
#include "ace/datasourcetimestatstable.hpp"


/* Utils. */
#include "ace/ldpc.hpp"

#include "ognpacket.hpp"

class Ogn1 : public BaseModule, public etl::message_router<Ogn1, GATAS::RadioRxManchesterMsg, GATAS::RadioTxPositionRequestMsg, GATAS::GpsStatsMsg>
{
public:
    static constexpr uint8_t OGN_PACKET_LENGTH = 20;
    static constexpr size_t OGN_PACKET_LENGTH_FEC = 26;

private:
    static constexpr uint32_t DEFAULT_IGNORE_DISTANCE = 25000;
    static constexpr uint32_t MAX_IGNORE_DISTANCE = 100000;
    friend class message_router;

    struct OGNAircraftType
    {
        enum enum_type : uint8_t
        {
            RESERVED_0 = 0,
            GLIDER = 1,
            TOW_PLANE = 2,
            ROTORCRAFT = 3,
            SKYDIVER = 4,
            DROP_PLANE = 5,
            HANG_GLIDER = 6,
            PARAGLIDER = 7,
            RECIP_ENGINE = 8,
            JET_TURBOPROP = 9,
            UNKNOWN = 10, // A
            BALLOON = 11, // B
            AIRSHIP = 12, // C
            UAV = 13,     // D
            RESERVED_E = 14,
            OBSTACLE = 15 // F
        };

        ETL_DECLARE_ENUM_TYPE(OGNAircraftType, uint8_t)
        ETL_ENUM_TYPE(RESERVED_0, "Reserved")
        ETL_ENUM_TYPE(GLIDER, "Glider")
        ETL_ENUM_TYPE(TOW_PLANE, "Tow Plane")
        ETL_ENUM_TYPE(ROTORCRAFT, "Helicopter")
        ETL_ENUM_TYPE(SKYDIVER, "Sky Diver")
        ETL_ENUM_TYPE(DROP_PLANE, "Drop Plane")
        ETL_ENUM_TYPE(HANG_GLIDER, "Hang Glider")
        ETL_ENUM_TYPE(PARAGLIDER, "Paraglider")
        ETL_ENUM_TYPE(RECIP_ENGINE, "Recip Engine Aircraft")
        ETL_ENUM_TYPE(JET_TURBOPROP, "Jet/Turboprop Aircraft")
        ETL_ENUM_TYPE(UNKNOWN, "Unknown")
        ETL_ENUM_TYPE(BALLOON, "Balloon")
        ETL_ENUM_TYPE(AIRSHIP, "Airship/Blimp")
        ETL_ENUM_TYPE(UAV, "Unmanned Aerial Vehicle")
        ETL_ENUM_TYPE(RESERVED_E, "Reserved")
        ETL_ENUM_TYPE(OBSTACLE, "Static Obstacle")
        ETL_END_ENUM_TYPE
    };

    struct
    {
        uint32_t receivedAircraftPositions = 0;
        uint32_t transmittedAircraftPositions = 0;
        uint32_t fecErr = 0;
        uint32_t outOfDistance = 0;
        uint32_t encrypted = 0;
        uint32_t nonPositional = 0;
        uint32_t relay[4] = {};
    } statistics;

    GATAS::DataSourceTimeStatsTable<2> datasourceTimeStats;

    const GATAS::OwnshipState &ownshipState;
    GATAS::GpsStats gpsStats{};
    uint32_t distanceIgnore;
    static LDPC_Decoder<Ogn1::OGN_PACKET_LENGTH * 8, 48> decoder;

public:
    static constexpr const etl::string_view NAME = "Ogn1";
    Ogn1(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), ownshipState(ownshipState_)
    {
        uint32_t di = config.valueByPath(DEFAULT_IGNORE_DISTANCE, NAME, "distanceIgnore");
        distanceIgnore = etl::clamp(di, static_cast<uint32_t>(0), MAX_IGNORE_DISTANCE);
    }

    // Safe only after failed postConstruct(), before start().
    // The packet-size check fails before any external resources are registered.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~Ogn1() = default;

    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

private:
    void on_receive(const GATAS::RadioRxManchesterMsg &msg);
    void on_receive(const GATAS::BarometricPressureMsg &msg);
    void on_receive(const GATAS::GpsStatsMsg &msg);
    void on_receive(const GATAS::RadioTxPositionRequestMsg &msg);
    void on_receive_unknown(const etl::imessage &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);

    // Transform a Ogn addressType to an gaTas address type
    GATAS::AddressType addressTypeFromOgn(uint8_t addressType) const;
    uint8_t addressTypeToOgn(GATAS::AddressType addressType) const;
    GATAS::AircraftCategory ognToGatas(Ogn1::OGNAircraftType o) const;
    Ogn1::OGNAircraftType gatasToOgn(GATAS::AircraftCategory c) const;

    int8_t parseFrame(OGN_Packet &packet, uint32_t frequency, int16_t rssiDbm);

    static uint8_t ErrCount(const uint8_t *err, uint8_t length);
    static uint8_t ErrCount(const uint8_t *output, const uint8_t *data, const uint8_t *err, uint8_t length);
    static uint8_t errorCorrect(uint8_t *output, uint8_t *data, uint8_t *err, uint8_t iter = 32);
};
