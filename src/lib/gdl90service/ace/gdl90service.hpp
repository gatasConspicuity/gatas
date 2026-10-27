#pragma once

#include <stdint.h>
#include <sys/time.h>

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/ownshipstate.hpp"

#include "GDL90.h"

/**
 * Client that can connect to a host and a port and expect to receive line terminated NMEA Messages
 * TODO: de-Couple UDP from the GDL90 so this service send GDL Message over the messagebus which can then be send over UDP or Serial or BlueToolh
 */
class Gdl90Service : public BaseModule, public etl::message_router<Gdl90Service, GATAS::EgressAircraftPositionMsg, GATAS::OwnshipPositionMsg, GATAS::GpsStatsMsg, GATAS::ConfigUpdatedMsg>
{
    friend class message_router;

private:
    mutable struct
    {
        uint32_t heartbeatTx = 0;
        uint32_t ownshipPosTx = 0;
        uint32_t trackingAircraftPosTx = 0;
        uint32_t trackingFailureErr = 0;
        uint32_t ownEncodingFailureErr = 0;
        uint32_t heartBeatEncodingFailureErr = 0;
        uint32_t packingFailureErr = 0;
    } statistics;

    enum TaskState : uint8_t
    {
        // Message to indicate next period needs to be decided, send from a timer
        HEARTBEAT = 1,
        SHUTDOWN = 2,
    };

    TaskHandle_t taskHandle = nullptr;
    GDL90 gdl90;

    GDL90::ADDR_TYPE type;
    const GATAS::OwnshipState &ownshipState;
    GATAS::GpsStats gpsStats;
    GATAS::CallSign ownshipCallsign;
    bool mslAltFallback = false;

private:
    static void gdl90ServiceTask(void *arg);

    void on_receive(const GATAS::EgressAircraftPositionMsg &msg);

    void on_receive(const GATAS::OwnshipPositionMsg &msg);
    void on_receive(const GATAS::GpsStatsMsg &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    void packAndSend(const GDL90::RawBytes &unpacked);
    void sendHeartBeat(Gdl90Service &gdl90Service);

    GDL90::NIC calcNIC(float hplMeters);
    GDL90::NACP calcNACp(float hfomMeters);
    uint32_t encodeAltitude(etl::optional<int32_t> altitudeMetres);

    GDL90::EMITTER aircraftTypeToEmitter(GATAS::AircraftCategory category) const;
    /**
     * Make a CALLSIGN suitable for GDL90 from the provided callsign
     * We only allow for - according to SW Mod D, and change all other character to a space
     */
    GATAS::CallSign makeGdlCallsign(const GATAS::CallSign &callSign) const;

public:
    static constexpr const etl::string_view NAME = "Gdl90Service";
    Gdl90Service(etl::imessage_bus &bus, const Configuration &config, const GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), ownshipState(ownshipState_)
    {
        (void)config;
        on_receive(GATAS::ConfigUpdatedMsg{config, Configuration::NAME});
    }

    virtual ~Gdl90Service() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
