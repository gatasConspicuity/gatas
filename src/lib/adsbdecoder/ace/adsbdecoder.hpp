#pragma once

#include <stdint.h>

#include "ace/constants.hpp"
#include "ace/models.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"

#include "adsbdatacollector.hpp"
#include "addresscache.hpp"

#include "mode-s.hpp"

#include "FreeRTOS.h"
#include "semphr.h"

/**
 * ADSBDecoder decodes ADSB message from a ADSB device and forwards that as NMEA Strings
 */
class ADSBDecoder : public BaseModule, etl::message_router<ADSBDecoder, GATAS::ConfigUpdatedMsg, GATAS::Every5SecMsg, GATAS::AdapativeRadiusMsg, GATAS::RadioRxMsg, GATAS::ADSBMessageBinMsg>
{
private:
    static constexpr uint8_t MAX_PLANES_TRACKED = 48;
    static constexpr uint8_t MAX_ADDRESS_CACHE_SIZE = 100;  // Address cache size for ignore planes that are to for/to high etc..
    static constexpr uint32_t ADSBDECODER_US_DELAY_SERIAL_AND_OVERHEAD = 20'000; // Delay in us to know 'exact' timing

    friend class message_router;

private:
    struct
    {
        uint32_t crcErrors = 0;
        uint32_t knownAircraftFull = 0;
        uint32_t ignoredAircraftFull = 0;
        uint32_t totalMsgReceived = 0;
        uint32_t totalMsgIgnored = 0;
        uint32_t msgMissed = 0;
    } statistics;


    AddressCache<MAX_ADDRESS_CACHE_SIZE, 15'000'000> ignoredAirplanes; // A quick cache to store all airplanes that we already know we should not track for up to 30 seconds
    AdsbDataCollector<MAX_PLANES_TRACKED, 5'000'000> adsbDataCollector;

    // Whole-second timestamps need six ticks to guarantee at least five elapsed seconds.
    static constexpr uint32_t ADSB_OFFSET_UPDATE_INTERVAL_SECONDS = 6;
    uint32_t lastAdsbOffsetUpdateSeconds = 0;

    SemaphoreHandle_t mutex;
    uint32_t filterRadius=100'000;
    GATAS::OwnshipState &ownshipState;
    mode_s_t state;
    int32_t filterAbove; // Filter out all aircraft above me in meters. 1000 means all aircraft 1000m or more above me will not get processed
    int32_t filterBelow; // Filter out all aircraft below me in meters. 100 means all aircraft 100m below me or more are not processed
public:
    static constexpr const etl::string_view NAME = "ADSBDecoder";
    ADSBDecoder(etl::imessage_bus &bus, const Configuration &config, GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME), mutex(nullptr), filterRadius(100000), ownshipState(ownshipState_)
    {
        filterAbove = config.valueByPath(true, NAME, "filterAbove");
        filterBelow = config.valueByPath(true, NAME, "filterBelow");
    }

    // Safe only after failed postConstruct(), before start().
    // Failure precedes decoder setup and leaves no allocated mutex.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~ADSBDecoder()
    {
    }

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

private:
    void getConfiguration(const Configuration &config);

    void on_receive(const GATAS::ConfigUpdatedMsg &msg);

    void on_receive(const GATAS::Every5SecMsg &msg);

    void on_receive(const GATAS::AdapativeRadiusMsg &msg);

    void on_receive(const GATAS::RadioRxMsg &msg);

    void on_receive(const GATAS::ADSBMessageBinMsg &msg);

    void processAdsbData(const uint8_t* data, size_t length, GATAS::TrafficIngress ingress, int16_t rssiDbm);

    bool outOfAltitudeRange(const GATAS::OwnshipPositionInfo &opi, int32_t otherellipseHeight);

    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    static GATAS::AircraftCategory getAircraftCategory(uint8_t typeCode, uint8_t category);
};
