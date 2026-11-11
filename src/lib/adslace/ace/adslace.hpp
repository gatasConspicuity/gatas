#pragma once

/* System. */
#include <stdint.h>

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

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"
#include "ace/basemodule.hpp"
#include "ace/datasourcetimestatstable.hpp"

#include "adsl/adsl.hpp"

/* Utils. */
// #include "ace/ldpc.hpp"
// #include "adsl_packet.hpp"

class ADSLAce : public BaseModule, ADSL::Connector, public etl::message_router<ADSLAce, GATAS::RadioRxManchesterMsg, GATAS::RadioRxMsg, GATAS::EgressAircraftPositionsMsg, GATAS::RadioTxPositionRequestMsg, GATAS::GpsStatsMsg>
{
    static constexpr int DEFAULT_IGNORE_DISTANCE = 25000;
    static constexpr int MAX_IGNORE_DISTANCE = 100000;

    friend class message_router;
    mutable struct
    {
        uint32_t receivedAircraftPositions = 0;
        uint32_t transmittedAircraftPositions = 0;
        uint32_t uplinksPacketsSend = 0;
        uint32_t uplinksPacketsReceived = 0;
        uint32_t fecErr = 0;
        uint32_t outOfDistance = 0;
        uint32_t encrypted = 0;
        uint32_t protocolVersionErr = 0;
        uint32_t decryptionKeyErr = 0;
        uint32_t payloadUnSupportedErr = 0;
        uint32_t unknownErr = 0;
        uint32_t unsupportedFec = 0;
    } statistics;

    ADSL::Protocol protocol;
    const GATAS::OwnshipState &ownshipState;

    GATAS::DataSourceTimeStatsTable<3> datasourceTimeStats;

    GATAS::GpsStats gpsStats;
    uint32_t distanceIgnore = MAX_IGNORE_DISTANCE;
    SemaphoreHandle_t protocolMutex;
    struct Tx_Struct
    {
        GATAS::RadioParameters radioParameters;
        uint8_t radioNo;
    };
    Tx_Struct rqMBandRadioParameters;
    Tx_Struct rqOBandRadioParameters;

public:
    static constexpr const etl::string_view NAME = "ADSL";
    ADSLAce(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME),
                                                                   protocol(this), ownshipState(ownshipState_)
    {
        (void) config;
        protocol.init();
        protocol.crcCheckOnReceive = false;
    }

    // Safe only after failed postConstruct(), before start().
    // The only failure is mutex allocation; protocol callbacks remain member-owned.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~ADSLAce() = default;

    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

private:
    /**
     * Send a FreeRTOS message when a ADSL is received
     * This will release the sender from the task and allow it to continue in a seperate thread
     */
    void on_receive(const GATAS::RadioRxManchesterMsg &msg);
    void on_receive(const GATAS::RadioRxMsg &msg);
    void on_receive(const GATAS::RadioTxPositionRequestMsg &msg);
    void on_receive(const GATAS::GpsStatsMsg &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::EgressAircraftPositionsMsg &msg);
    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    // Transform a FLARM addressType to an gaTas address type
    GATAS::AddressType addressMapToAddressType(uint8_t addressMap) const;
    static uint8_t addressTypeToAddressMap(GATAS::AddressType addressType);
    static GATAS::AircraftCategory mapAircraftCategory(ADSL::TrafficPayload::AircraftCategory category);
    static ADSL::TrafficPayload::AircraftCategory mapAircraftCategory(GATAS::AircraftCategory category);


    // ADSL Protocol Handler functions
    virtual uint32_t adsl_getTick() const
    {
        return CoreUtils::timeUs32();
    }

    virtual bool adsl_sendFrame(const void *ctx, const uint8_t *data, size_t lengthBytes) override;

    // Called when a payload/header/status is received by the protocol layer
    virtual void adsl_receivedTraffic(const ADSL::Header &header, const ADSL::TrafficPayload &tp) override;
    virtual void adsl_receivedStatus(const ADSL::Header &header, const ADSL::StatusPayload &tp) override;
    virtual void adsl_receivedUplinkTraffic(const ADSL::Header &header, etl::span<const ADSL::UplinkEntry> entries) override;
    virtual void adsl_buildStatusPayload(const void *ctx, ADSL::StatusPayload &sp) override;

    virtual uint8_t *adsl_alloc(const void *ctx, size_t sizeBytes);

    virtual void adsl_lock() override;
    virtual void adsl_unlock() override;

    ADSL::TrafficPayload buildOwnshipTrafficPayload(const GATAS::OwnshipPositionInfo &ownship);
    ADSL::TrafficPayload buildTrafficPayload(const GATAS::AircraftPositionInfo &traffic);
    ADSL::StatusPayload buildOwnshipStatusPayload();
    ADSL::TrafficPayload::HorizontalPositionAccuracy mapHFOM(float hfomMeters);
    ADSL::TrafficPayload::NavigationIntegrity mapHPL(float hplMeters);
    ADSL::TrafficPayload::VerticalPositionAccuracy mapVFOM(float vfomMeters);
    ADSL::TrafficPayload::VelocityAccuracy mapVelAcc(float accVelMps);
};
