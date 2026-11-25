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
    uint32_t lastSeenSeconds; // Monotonic seconds since boot
    int32_t reportedAltitudeMetres;
    int32_t pressureAlt = GATAS::INVALID_BARO_ALTITUDE; // Standard pressure altitude in metres
    int32_t raw_even_latitude;  // Non decoded latitude  even
    int32_t raw_even_longitude; // Non decoded longitude even
    int32_t raw_odd_latitude;   // Non decoded latitude   odd
    int32_t raw_odd_longitude;  // Non decoded longitude  odd
    float lat;                  // lat
    float lon;                  // lon
    GATAS::CallSign callSign;
    etl::optional<int16_t> gnssMinusBaroMetres;
    uint16_t velocity;          // Ground speed in knots
    int16_t squawk;             // Mode A squawk, -1 when unknown
    int16_t heading;            // Heading in degrees
    int16_t vert_rate;          // Vertical Rate
    int16_t positionRssiDbm = INT16_MIN;
    uint8_t messageStatus; // How complete this message is
    GATAS::AircraftCategory category = GATAS::AircraftCategory::UNKNOWN;
    GATAS::TrafficIngress positionIngress = GATAS::TrafficIngress::Unknown;
    uint8_t pressureAltAgeSeconds = 0; // Whole second boundaries crossed as of lastSeenSeconds
    uint8_t flags = 0;

    static constexpr uint8_t ALTITUDE_IS_PRESSURE = 1 << 0;
    static constexpr uint8_t EVICT = 1 << 1;

    static constexpr uint32_t US_PER_SECOND = 1'000'000;
    static constexpr uint8_t MAX_PRESSURE_AGE_SECONDS = 5;

    int32_t ellipseHeight() const
    {
        if ((flags & ALTITUDE_IS_PRESSURE) == 0)
        {
            return reportedAltitudeMetres;
        }
        if (gnssMinusBaroMetres)
        {
            return reportedAltitudeMetres + *gnssMinusBaroMetres;
        }
        // Without the reported difference, approximate MSL with pressure altitude.
        return reportedAltitudeMetres + CoreUtils::egmGeoidOffset(lat, lon);
    }

    bool shouldEvict() const
    {
        return (flags & EVICT) != 0;
    }

    void markForEviction()
    {
        flags |= EVICT;
    }

    void updateLastSeen(uint32_t nowSeconds)
    {
        const uint32_t elapsed = nowSeconds - lastSeenSeconds;
        pressureAltAgeSeconds += etl::min<uint32_t>(elapsed, UINT8_MAX - pressureAltAgeSeconds);
        lastSeenSeconds = nowSeconds;
    }

    ~AdsbCombinedDataStatus() = default;

    AdsbCombinedDataStatus() = delete;

    // Constructor with icao for search functions.
    AdsbCombinedDataStatus(uint32_t icao_)
        : lastSeenSeconds(0), reportedAltitudeMetres(0), raw_even_latitude(0),
          raw_even_longitude(0), raw_odd_latitude(0), raw_odd_longitude(0),
          lat(0.0f), lon(0.0f), callSign(""), velocity(0), squawk(-1), heading(0), vert_rate(0), messageStatus(0)
    {
        (void)icao_;
    }

    // Constructor with icao and lastSeen parameters
    AdsbCombinedDataStatus(uint32_t icao_, uint32_t lastSeenSeconds_)
        : lastSeenSeconds(lastSeenSeconds_), reportedAltitudeMetres(0), raw_even_latitude(0),
          raw_even_longitude(0), raw_odd_latitude(0), raw_odd_longitude(0),
          lat(0.0f), lon(0.0f), callSign(""), velocity(0), squawk(-1), heading(0), vert_rate(0), messageStatus(0)
    {
        (void)icao_;
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


    bool updatePosition(bool fflag, int16_t rssiDbm)
    {
        auto &aircraft = *currentDataStatus;
        constexpr uint8_t hasBothPositions = HAS_POSITION_ODD | HAS_POSITION_EVEN;
        if ((aircraft.messageStatus & hasBothPositions) != hasBothPositions)
        {
            return false;
        }
        if (const auto position = decodeCPR(fflag, aircraft.raw_even_latitude, aircraft.raw_even_longitude,
                                           aircraft.raw_odd_latitude, aircraft.raw_odd_longitude))
        {
            aircraft.lat = position->lat;
            aircraft.lon = position->lon;
            aircraft.positionRssiDbm = rssiDbm;
            aircraft.messageStatus |= HAS_POSITION_UPDATED;
            return true;
        }
        return false;
    }

    AdsbCombinedDataStatus *currentDataStatus = nullptr;

public:
    AdsbDataCollector() : currentDataStatus(nullptr) {}

    void clear()
    {
        cache.clear();
    }
    bool acceptsIngress(uint32_t address, GATAS::TrafficIngress ingress) const
    {
        const auto entry = cache.find(address);
        return entry == cache.end() || entry->second.positionIngress == GATAS::TrafficIngress::Unknown ||
               entry->second.positionIngress == ingress;
    }

    bool start(uint32_t address, uint64_t usTime)
    {
        const auto nowSeconds = static_cast<uint32_t>(usTime / AdsbCombinedDataStatus::US_PER_SECOND);
        auto itf = cache.find(address);
        if (itf != cache.end())
        {
            currentDataStatus = &itf->second;
            currentDataStatus->updateLastSeen(nowSeconds);
            return true;
        }

        if (!cache.full()) {
            auto [iti, inserted] = cache.insert({address, AdsbCombinedDataStatus{address, nowSeconds}});
            if (inserted)
            {
                currentDataStatus = &iti->second;
            }
            return inserted;
        }

        return false;
    }

    void evictOldEntries(uint64_t usTime)
    {
        constexpr int32_t EVICT_TIME_MINIMUM = 2'000'000; // Expected to have at least every second an update from ADSB
        int32_t evictTime = EVICT_TIME_US;
        while ((cache.size() > CLEAR_UP_SIZE) && evictTime > EVICT_TIME_MINIMUM)
        {
            for (auto it = cache.cbegin(); it != cache.cend();)
            {
                const auto lastSeenUs = uint64_t{it->second.lastSeenSeconds} * AdsbCombinedDataStatus::US_PER_SECOND;
                if (it->second.shouldEvict() || usTime - lastSeenUs > static_cast<uint32_t>(evictTime))
                {
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
        const auto nowSeconds = CoreUtils::monotonic() / AdsbCombinedDataStatus::US_PER_SECOND;
        for (const auto &entry : cache)
        {
            const auto &data = entry.second; // Access the value part of the pair
            printf("icao:%06X status:%02X elapsedSeconds:%lu address:%s gnssAltitude: %d\n", entry.first, data.messageStatus, static_cast<unsigned long>(nowSeconds - data.lastSeenSeconds), data.icaoAddress.c_str(), data.ellipseHeight());
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

    int32_t pressureAltitudeAt(uint64_t nowUs) const
    {
        const auto &aircraft = *currentDataStatus;
        const uint64_t sampleSecond = aircraft.lastSeenSeconds - aircraft.pressureAltAgeSeconds;
        const uint64_t deadlineUs = (sampleSecond + AdsbCombinedDataStatus::MAX_PRESSURE_AGE_SECONDS) * AdsbCombinedDataStatus::US_PER_SECOND;
        return nowUs <= deadlineUs ? aircraft.pressureAlt : GATAS::INVALID_BARO_ALTITUDE;
    }

    void updatePressureAltitude(int32_t metres)
    {
        auto &aircraft = current();
        aircraft.pressureAlt = metres;
        aircraft.pressureAltAgeSeconds = 0;
        aircraft.messageStatus |= HAS_ALTITUDE;
        aircraft.reportedAltitudeMetres = metres;
        aircraft.flags |= AdsbCombinedDataStatus::ALTITUDE_IS_PRESSURE;
    }

    void updateGnssAltitude(int32_t altitude)
    {
        currentDataStatus->messageStatus |= HAS_ALTITUDE;
        currentDataStatus->reportedAltitudeMetres = altitude;
        currentDataStatus->flags &= static_cast<uint8_t>(~AdsbCombinedDataStatus::ALTITUDE_IS_PRESSURE);
    }

    void updateCallsign(const GATAS::IcaoAddress &flight, GATAS::AircraftCategory category)
    {
        currentDataStatus->category = category;
        if (!(currentDataStatus->messageStatus & CHECK_HAS_CALLSIGN))
        {
            currentDataStatus->messageStatus |= CHECK_HAS_CALLSIGN;
            currentDataStatus->callSign = flight;
            etl::trim_whitespace(currentDataStatus->callSign);
        }
    }

    void updateSquawk(uint16_t squawk)
    {
        currentDataStatus->squawk = static_cast<int16_t>(squawk);
    }

    bool updateRawOdd(uint32_t raw_latitude, uint32_t raw_longitude, int16_t rssiDbm)
    {
        currentDataStatus->messageStatus |= HAS_POSITION_ODD;
        currentDataStatus->raw_odd_latitude = raw_latitude;
        currentDataStatus->raw_odd_longitude = raw_longitude;
        return updatePosition(true, rssiDbm);
    }

    bool updateRawEven(uint32_t raw_latitude, uint32_t raw_longitude, int16_t rssiDbm)
    {
        currentDataStatus->messageStatus |= HAS_POSITION_EVEN;
        currentDataStatus->raw_even_latitude = raw_latitude;
        currentDataStatus->raw_even_longitude = raw_longitude;
        return updatePosition(false, rssiDbm);
    }

    void updateHeading(int16_t heading)
    {
        currentDataStatus->messageStatus |= HAS_HEADING;
        currentDataStatus->heading = heading;
    }

    void updateVelocityHeading(uint16_t velocity, int16_t vert_rate, int16_t heading)
    {
        currentDataStatus->messageStatus |= HAS_HEADING | HAS_VELOCITY;
        currentDataStatus->velocity = velocity;
        currentDataStatus->vert_rate = vert_rate;
        currentDataStatus->heading = heading;
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
