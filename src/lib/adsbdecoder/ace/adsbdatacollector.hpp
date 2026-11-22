#pragma once

#include "ace/coreutils.hpp"
#include "ace/constants.hpp"
#include "ace/models.hpp"

#include "etl/unordered_map.h"
#include "etl/hash.h"
#include "etl/string_utilities.h"

#include "cpr.hpp"
#include "iaddresscache.hpp"

struct AdsbCombinedDataStatus
{
    uint32_t icao; // ICAO address
    GATAS::CallSign callSign;
    uint8_t messageStatus; // How complete this message is
    uint32_t lastSeen;

    // Data from ADSB
    uint16_t velocity;          // Non decoded Ground speed in knots
    uint8_t category;           // category
    int16_t squawk;             // Mode A squawk, -1 when unknown
    int16_t heading;            // Heading in degrees
    int32_t ellipseHeight;        // Altitude in meter
    int32_t raw_even_latitude;  // Non decoded latitude  even
    int32_t raw_even_longitude; // Non decoded longitude even
    int32_t raw_odd_latitude;   // Non decoded latitude   odd
    int32_t raw_odd_longitude;  // Non decoded longitude  odd
    int16_t baro_gnss_diff;     // Difference between barometric altitude and GNSS altitude
    float lat;                  // lat
    float lon;                  // lon
    int16_t vert_rate;          // Vertical Rate
    bool airborne;              // true when airplane is airborne
    bool evict;                 // When set to true, the aircraft needs to be removed from cache in the next evict cycle
    GATAS::TrafficIngress oddIngress = GATAS::TrafficIngress::Unknown;
    GATAS::TrafficIngress evenIngress = GATAS::TrafficIngress::Unknown;
    GATAS::TrafficIngress positionIngress = GATAS::TrafficIngress::Unknown;
    int16_t positionRssiDbm = INT16_MIN;

    ~AdsbCombinedDataStatus() = default;

    AdsbCombinedDataStatus() = delete;

    // Constructor with icao for search functions.
    AdsbCombinedDataStatus(uint32_t icao_)
        : icao(icao_), callSign(""), messageStatus(0), lastSeen(0),
          velocity(0.0f), category(0), squawk(-1), heading(0), ellipseHeight(0), raw_even_latitude(0),
          raw_even_longitude(0), raw_odd_latitude(0), raw_odd_longitude(0), baro_gnss_diff(0),
          lat(0.0f), lon(0.0f), vert_rate(0.0f), airborne(false), evict(false)
    {
    }

    // Constructor with icao and lastSeen parameters
    AdsbCombinedDataStatus(uint32_t icao_, uint32_t lastSeen_)
        : icao(icao_), callSign(""), messageStatus(0), lastSeen(lastSeen_),
          velocity(0.0f), category(0), squawk(-1), heading(0), ellipseHeight(0), raw_even_latitude(0),
          raw_even_longitude(0), raw_odd_latitude(0), raw_odd_longitude(0), baro_gnss_diff(0),
          lat(0.0f), lon(0.0f), vert_rate(0.0f), airborne(false), evict(false)
    {
    }

};

/**
 * Performance measurements calling the start method:
 * flat map took 16us
 * unordered_map takes 5us
 * EVICT_TIME_US should not be below 2'000'000
 */
template <size_t SIZE, int32_t EVICT_TIME_US>
class AdsbDataCollector
{
    static constexpr uint8_t HAS_POSITION_ODD = 1 << 0;                    //
    static constexpr uint8_t HAS_POSITION_EVEN = 1 << 1;                   //
    static constexpr uint8_t HAS_HEADING = 1 << 2;                         //
    static constexpr uint8_t HAS_VELOCITY = 1 << 3;
    static constexpr uint8_t HAS_ALTITUDE = 1 << 4; //
    static constexpr uint8_t HAS_POSITION_UPDATED = 1 << 5;
    static constexpr uint8_t NOT_HAS_POSITION_UPDATED = static_cast<uint8_t>(~HAS_POSITION_UPDATED);
    static constexpr uint8_t CHECK_HAS_CALLSIGN = 1 << 6;
    static constexpr uint8_t VALID_MASK = HAS_POSITION_ODD | HAS_POSITION_EVEN | HAS_HEADING | HAS_VELOCITY | HAS_ALTITUDE | HAS_POSITION_UPDATED;

    static constexpr uint32_t CLEAR_UP_SIZE = (SIZE * 90) / 100;

private:
    etl::unordered_map<uint32_t, AdsbCombinedDataStatus, SIZE, SIZE> cache;


    void updatePosition(bool fflag, int16_t rssiDbm)
    {
        auto &aircraft = *currentDataStatus;
        constexpr uint8_t hasBothPositions = HAS_POSITION_ODD | HAS_POSITION_EVEN;
        if ((aircraft.messageStatus & hasBothPositions) != hasBothPositions)
        {
            return;
        }
        if (const auto position = decodeCPR(fflag, aircraft.raw_even_latitude, aircraft.raw_even_longitude,
                                           aircraft.raw_odd_latitude, aircraft.raw_odd_longitude))
        {
            aircraft.lat = position->lat;
            aircraft.lon = position->lon;
            aircraft.positionIngress = aircraft.oddIngress == aircraft.evenIngress
                                           ? aircraft.oddIngress : GATAS::TrafficIngress::Mixed;
            aircraft.positionRssiDbm = rssiDbm;
            aircraft.messageStatus |= HAS_POSITION_UPDATED;
        }
    }

    AdsbCombinedDataStatus *currentDataStatus = nullptr;

public:
    AdsbDataCollector() : currentDataStatus(nullptr) {}

    void clear()
    {
        cache.clear();
    }
    bool start(uint32_t address, uint32_t usTime)
    {
        auto itf = cache.find(address);
        if (itf != cache.end())
        {
            currentDataStatus = &itf->second;
            currentDataStatus->lastSeen = usTime;
            return true;
        }

        if (!cache.full()) {
            auto [iti, inserted] = cache.insert({address, AdsbCombinedDataStatus{address, usTime}});
            if (inserted)
            {
                currentDataStatus = &iti->second;
            }
            return inserted;
        }

        return false;
    }

    void evictOldEntries(uint32_t usTime)
    {

        constexpr int32_t EVICT_TIME_MINIMUM = 2'000'000; // Expected to have at least every second an update from ADSB
        int32_t evictTime = EVICT_TIME_US;
        while ((cache.size() > CLEAR_UP_SIZE) && evictTime > EVICT_TIME_MINIMUM)
        {
            for (auto it = cache.cbegin(); it != cache.cend();)
            {
                if (it->second.evict || (-CoreUtils::usToReferenceRaw(it->second.lastSeen, usTime) > evictTime))
                {
                    // printf("Evict: icao:%06X lastSee:%ld usTime:%ld, diff:%ld\n", it->second.icao, it->second.lastSeen, usTime, CoreUtils::usFromReference(it->second.lastSeen, usTime));
              //      printf(".");
                    it = cache.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            evictTime -= EVICT_TIME_US / 4;
             //       printf(" %ld ", evictTime);
        }
    }

    void dump()
    {
        auto usTime = CoreUtils::timeUs32Raw();
        for (const auto &entry : cache)
        {
            const auto &data = entry.second; // Access the value part of the pair
            printf("icao:%06X status:%02X elsapsed:%06d address:%s gnssAltitude: %d\n", data.icao, data.messageStatus, CoreUtils::usToReferenceRaw(data.lastSeen, usTime) / 1000, data.icaoAddress.c_str(), data.ellipseHeight);
        }
    }

    size_t size() const
    {
        return cache.size();
    }

    inline AdsbCombinedDataStatus &current()
    {
        return *currentDataStatus;
    }

    void updateAltitude(int32_t altitude)
    {
        currentDataStatus->messageStatus |= HAS_ALTITUDE;
        currentDataStatus->ellipseHeight = altitude + currentDataStatus->baro_gnss_diff;
    }

    void updateGnssAltitude(int32_t altitude)
    {
        currentDataStatus->messageStatus |= HAS_ALTITUDE;
        currentDataStatus->ellipseHeight = altitude;
    }

    void updateCallsign(const GATAS::IcaoAddress &flight, uint8_t aircraft_type)
    {
        (void)flight;
        if (!(currentDataStatus->messageStatus & CHECK_HAS_CALLSIGN))
        {
            currentDataStatus->messageStatus |= CHECK_HAS_CALLSIGN;
            currentDataStatus->callSign = flight;
            etl::trim_whitespace(currentDataStatus->callSign);
            currentDataStatus->category = aircraft_type;
        }
    }

    void updateSquawk(uint16_t squawk)
    {
        currentDataStatus->squawk = static_cast<int16_t>(squawk);
    }

    void updateRawOdd(uint32_t raw_latitude, uint32_t raw_longitude, GATAS::TrafficIngress ingress, int16_t rssiDbm)
    {
        currentDataStatus->messageStatus |= HAS_POSITION_ODD;
        currentDataStatus->raw_odd_latitude = raw_latitude;
        currentDataStatus->raw_odd_longitude = raw_longitude;
        currentDataStatus->oddIngress = ingress;
        updatePosition(true, rssiDbm);
    }

    void updateRawEven(uint32_t raw_latitude, uint32_t raw_longitude, GATAS::TrafficIngress ingress, int16_t rssiDbm)
    {
        currentDataStatus->messageStatus |= HAS_POSITION_EVEN;
        currentDataStatus->raw_even_latitude = raw_latitude;
        currentDataStatus->raw_even_longitude = raw_longitude;
        currentDataStatus->evenIngress = ingress;
        updatePosition(false, rssiDbm);
    }

    void updateAirborne(bool airborne)
    {
        currentDataStatus->airborne = airborne;
    }

    void updateHeading(int16_t heading)
    {
        currentDataStatus->messageStatus |= HAS_HEADING;
        currentDataStatus->heading = heading;
    }

    void updateVelocityHeadingBaroDiff(uint16_t velocity, int16_t vert_rate, int16_t heading, int16_t baro_gnss_diff)
    {
        currentDataStatus->messageStatus |= HAS_HEADING | HAS_VELOCITY;
        currentDataStatus->velocity = velocity;
        currentDataStatus->vert_rate = vert_rate;
        currentDataStatus->heading = heading;
        currentDataStatus->baro_gnss_diff = baro_gnss_diff;
    }

    bool positionUpdatedAndValid()
    {
        if ((currentDataStatus->messageStatus & VALID_MASK) == VALID_MASK)
        {
            currentDataStatus->messageStatus &= NOT_HAS_POSITION_UPDATED;
            return true;
        }

        return false;
    }
};
