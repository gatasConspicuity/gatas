#pragma once

#include <stdint.h>

#include "ace/constants.hpp"
#include "ace/models.hpp"

#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"
#include "ace/EMA.hpp"

#include "minmea.h"

#include "etl/math.h"

/**
 * This decoder requires that both GGA and GMC sentences are received from the GPS and that each sentences us correct ms resolution
 * When both coralated sentences are sned. It will send out a ownship position message
 */
class GpsDecoder : public BaseModule, public etl::message_router<GpsDecoder, GATAS::GPSSentenceMsg, GATAS::ConfigUpdatedMsg>
{
    friend class message_router;
    struct
    {
        uint32_t receivedGGA = 0;
        uint32_t receivedRMC = 0;
        uint32_t receivedGSA = 0;
        uint32_t receivedGSV = 0;
        uint32_t receivedOther = 0;
        uint32_t startTime = CoreUtils::timeS32();
    } statistics;

    RatePerSecond altitudeGeoid{GATAS_EMAFLOAT_K_FACTOR_2PS, 2}; // Field 9
    float geoidSeparation = 0;                                   // Field 11
    float groundSpeed = 0;
    RatePerSecond course{GATAS_EMAFLOAT_K_FACTOR_2PS, 2};
    float latitude = 0;
    float longitude = 0;

    // 0: Fix not valid
    // 1: GPS fix
    // 2: Differential GPS fix (DGNSS), SBAS, OmniSTAR VBS, Beacon, RTX in GVBS mode
    // 3: Not applicable
    // 4: RTK Fixed, xFill
    // 5: RTK Float, OmniSTAR XP/HP, Location RTK, RTX
    // 6: INS Dead reckoning
    struct SatViewStats
    {
        uint8_t gps = 0;
        uint8_t glo = 0;
        uint8_t gal = 0;
        uint8_t bds = 0;
    } satViewStats;

    uint8_t satsUsedForFix;
    float pDop;
    float hDop;
    float vDop;
    minmea_time lastRMCTimestamp;
    minmea_time lastGGATimestamp;
    const uint32_t taskStartTime;
    uint8_t fixQuality;
    GATAS::GpsFixType fixType;
    GATAS::Config::Conspicuity conspicuity;
    int16_t heightAboveGps = 0;
    bool groundStation = false;
    GATAS::OwnshipState &ownshipState;
private:
    void on_receive(const GATAS::GPSSentenceMsg &msg);
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);

    /**
     * Convert an minmea_float with altitude/height information in meters
     */
    float convertToMeters(const minmea_float &value, char unit, float defaultValue) const;

    /**
     * Send message when both GGA and RMC sentences are received
     */
    void sendMessageWhenGGAisRMC();

    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    uint8_t getGpsRate() const
    {
        return (((float)statistics.receivedRMC) / (CoreUtils::timeS32() - taskStartTime)) + 0.5f;
    }

    float getFloat(const minmea_float &f, float defaultValue)
    {
        if (f.scale == 0)
        {
            return defaultValue;
        }
        return minmea_tofloat(&f);
    }

public:
    static constexpr const etl::string_view NAME = "GpsDecoder";
    GpsDecoder(etl::imessage_bus &bus, const Configuration &config, GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME),
                                                                      satsUsedForFix(0),
                                                                      pDop(255),
                                                                      hDop(255),
                                                                      lastRMCTimestamp({0, 0, 0, 0}),
                                                                      lastGGATimestamp({0, 0, 0, 0}),
                                                                      taskStartTime(CoreUtils::timeS32()),
                                                                      fixQuality(0),
                                                                      fixType(GATAS::GpsFixType::NO_FIX),
                                                                      ownshipState(ownshipState_)
    {
        conspicuity = config.gaTasConfig().conspicuity;
        heightAboveGps = conspicuity.heightAboveGps;
        groundStation = conspicuity.groundStation;
    }

    // Safe only after failed postConstruct(), before start().
    // Construction and postConstruct only initialise owned state.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~GpsDecoder() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
