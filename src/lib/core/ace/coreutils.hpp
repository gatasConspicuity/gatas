#pragma once

#include "constants.hpp"
#include <stdint.h>
#include <time.h>
#include <type_traits>

#include "pico/time.h"

#include "messages.hpp"
#include "spinlockguard.hpp"

#include "etl/string.h"
#include "etl/optional.h"
#include "etl/vector.h"

namespace CoreUtils
{
    void init();

    spin_lock_t *sharedSpinLock();

    /** Resolve a traffic altitude in metres using the ownship pressure reference.
     *
     * Egress pressure | Ownship pressure | MSL fallback | Egress altitude
     * Available       | Any              | Any          | Egress pressure
     * Missing         | Available        | Any          | Ownship pressure + egress HAE - ownship HAE
     * Missing         | Missing          | Enabled      | Egress HAE - ownship geoid separation
     * Missing         | Missing          | Disabled     | Unavailable
     *
     * HAE values are assumed to be suitable for comparison. MSL fallback is a
     * compatibility option and does not produce a measured pressure altitude.
     */
    inline etl::optional<int32_t> resolveTrafficAltitude(const GATAS::AircraftPositionInfo &position,
                                                         const GATAS::OwnshipState &ownshipState,
                                                         bool allowMslFallback)
    {
        if (position.isPressureAltitudeValid())
        {
            return position.pressureAlt;
        }

        const auto ownship = ownshipState.location.load();
        // Use a wider intermediate so out-of-range inputs cannot overflow during subtraction.
        int64_t altitudeMetres = position.ellipseHeight;
        if (const auto ownshipPressureAltitude = ownshipState.calculatePressureAltitude())
        {
            altitudeMetres += static_cast<int32_t>(ownshipPressureAltitude.value()) - static_cast<int64_t>(ownship.ellipseHeight);
        }
        else if (allowMslFallback)
        {
            altitudeMetres -= ownship.geoidSeparation;
        }
        else
        {
            return etl::nullopt;
        }

        if (altitudeMetres < INT32_MIN || altitudeMetres > INT32_MAX)
        {
            return etl::nullopt;
        }
        return static_cast<int32_t>(altitudeMetres);
    }

    /**
     * Get a timestamp in us aligned on PPS so that for example45000 is at the exact pps
     * This timestamp monotonically increases from power up
     * this works together with isReached to measure time differences
     * Use this to measure short time differences of less then 71minutes
     * \sa isReached
     */
    uint32_t timeUs32();

    inline uint32_t timeUs32Raw()
    {
        return time_us_32();
    }

    /**
     * Get a monotonic 64-bit timestamp in microseconds.
     *
     * Unlike timeUs64(), this value is not adjusted when PPS alignment changes,
     * making it suitable for measuring elapsed time and scheduling timeouts.
     */
    inline uint64_t monotonic()
    {
        return time_us_64();
    }

    inline uint32_t monotonic32()
    {
        return time_us_32();
    }

    /**
     * Get a 64-bit timestamp in us aligned on PPS.
     * This uses the same hardware time base as timeUs32(), but preserves the full
     * 64-bit range for long-running intervals and epoch-related calculations.
     * The timestamp monotonically increases from power up.
     */
    uint64_t timeUs64();

    /**
     * Get a timestamp in ms
     * This timestamp monotonically increases from power up and alligned with PPS
     * \sa timeUs32
     */
    inline uint32_t timeMs32()
    {
        return timeUs64() / 1'000;
    }

    /**
     * Get a timestamp in seconds
     * This timestamp monotonically increases from power up and alligned with PPS
     */
    inline uint32_t timeS32()
    {
        return timeUs64() / 1'000'000;
    }

    /**
     * Calculate the time from referenceUs to us
     * If referenceUs is in the past, the result is negative, eg the event happened
     */
    inline int32_t usToReference(uint32_t referenceUs, uint32_t now = timeUs32())
    {
        auto diff = static_cast<int32_t>(referenceUs - now);
#if GATAS_DEBUG == 1
        if (diff < -15 * 60 * 1'000'000 || diff > 15 * 60 * 1'000'000)
        {
            // These Routines usToReferenceRaw/usToReference/usDiff/isUsReached/isUsReachedRaw/timeUs32/timeUs32Raw are designed
            // for short durations only, eg what will happen in the next second, or perhaps a few minutes. They are not suitable for
            // very long delays. They can only be used up to 30minutes maximum before counters start to wrap over (wrap over is at ~ 1.2hour)
            GATAS_VERIFY(false, "WARNING: Large references far in the future are not expected from usToReference, please check code.");
        }
#endif
        return diff;
    }

    inline int32_t usToReferenceRaw(uint32_t referenceUsRaw, uint32_t us = timeUs32Raw())
    {
        auto diff = static_cast<int32_t>(referenceUsRaw - us);
#if GATAS_DEBUG == 1
        if (diff < -15 * 60 * 1'000'000 || diff > 15 * 60 * 1'000'000)
        {
            // These Routines usToReferenceRaw/usToReference/usDiff/isUsReached/isUsReachedRaw/timeUs32/timeUs32Raw are designed
            // for short durations only, eg what will happen in the next second, or perhaps a few minutes. They are not suitable for
            // very long delays. They can only be used up to 30minutes maximum before counters start to wrap over (wrap over is at ~ 1.2hour)
            // This is for developers only
            GATAS_VERIFY(false, "WARNING: Large references far in the future are not expected from usToReferenceRaw, please check code.");
        }
#endif
        return diff;
    }

    inline int32_t usDiff(uint32_t referenceUs, uint32_t us = timeUs32())
    {
        return etl::absolute(usToReference(referenceUs, us));
    }

    /**
     * Decide if the referenced time is reached
     * Use this to measure short time differences of less then 71minutes
     * It will properly handle wraparounds if the time is less than 35 minutes in difference
     * \sa timeUs32
     */
    inline bool isUsReached(uint32_t referenceUs, uint32_t us = timeUs32())
    {
        return usToReference(referenceUs, us) <= 0;
    }
    inline bool isUsReachedRaw(uint32_t referenceUs, uint32_t us = timeUs32Raw())
    {
        return usToReferenceRaw(referenceUs, us) <= 0;
    }

    /**
     * Must be called at high priority to set the PPS offset.
     * When offset is known, the correct time in us in reference to PPS can be calculated
     * @param offsetUs Offset in us to add to the current time_us_32 to align with PPS This can be used for software PPS adjustments
     */
    void __time_critical_func(setPPS)(int32_t offsetUs);

    /**
     * Returns the current ms within the current second and alligned with PPS
     * eg: a value of 119 means 119ms since PPS
     */
    inline uint16_t msInSecond()
    {
        return (timeUs32() / 1'000) % 1'000;
    }

    inline uint32_t usInSecond()
    {
        return timeUs64() % 1'000'000;
    }

    /**
     * Calculate the time in ms to the next refMsInSecond time
     * Example: current time in msInSecond is 600
     * When reference time is 700, the result is 100
     * When reference time is 200, the result is 600 (Wraps over a whole second)
     * Currently will only allow to delay for 1000ms max
     * ms needs to be 0..1000
     * refMsInSecond 0..n
     */
    inline uint16_t msDelayToReference(uint16_t refMsInSecond, uint16_t ms = msInSecond())
    {
        if (refMsInSecond > ms)
        {
            return refMsInSecond - ms;
        }
        else
        {
            return 1'000 - ms + refMsInSecond;
        }
    }

    //////////////// EPOCH functions ////////////////
    /**
     * Set's the offset to the current time in ms since epoch.
     * THis function should be called to sync GPS time to the PICO's time using the PPS from the GPS
     * and is given the exact epochoffset when received from PPS this will calibrate the epoch function to exact ms
     * See RTC::on_receive(const GATAS::UtcTimeMsg& msg)
     */
    void setOffsetMsSinceEpoch(uint64_t msSinceEpoch);

    /**
     * Returns the current time in ms since epoch
     */
    uint64_t msSinceEpoch();

    /**
     * Seconds since EPOCH, like unix time
     */
    inline uint32_t secondsSinceEpoch()
    {
        return msSinceEpoch() / 1000;
    }

    /**
     * Seconds since midnight UTC (0–86399).
     * Uses a dedicated 32-bit offset captured in setOffsetMsSinceEpoch; returns 0 until that is called.
     */
    inline uint32_t secondsSinceMidnight()
    {
        return secondsSinceEpoch() % 86'400;
    }

    inline uint32_t msSinceMidnight()
    {
        return msSinceEpoch() % 86'400'000;
    }

    /**
     * Convert a UTC millisecond-in-minute hint into the local PPS-aligned
     * timeUs32() frame.
     *
     * @return The converted timestamp, or etl::nullopt when epoch sync is
     * unavailable or the hint is outside the allowed age window.
     */
    etl::optional<uint32_t> timeUs32FromMsInMinute(uint16_t msInMinute, uint16_t maxAbsDeltaMs = 10'000);

    inline tm localTime(uint64_t msSinceEpoch)
    {
        time_t secondsSinceEpoch = msSinceEpoch / 1000;
        struct tm timeinfo = {};
        localtime_r(&secondsSinceEpoch, &timeinfo);
        return timeinfo;
    }

    inline tm localTime()
    {
        return localTime(msSinceEpoch());
    }
    //////////////// EPOCH functions ////////////////

    /**
     * Get the current time in hours, minutes and seconds since mightnight UTC based on epoch
     */
    inline auto hourMinutesSeconds(uint32_t now = secondsSinceEpoch())
    {
        struct HourMinuteSeconds
        {
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
        };

        uint8_t h = (uint8_t)((now / 3600) % 24);
        uint8_t m = (uint8_t)((now / 60) % 60);
        uint8_t s = (uint8_t)(now % 60);

        return HourMinuteSeconds{h, m, s};
    }

    /**
     * Calculate distance between two points on earth using haversine
     * For a fast method check distanceFast and getDistanceRelNorthRelEastFloat
     *
     * returns distance in meters
     * Reference: https://geopy.readthedocs.io/en/stable/#module-geopy.distance
     * TODO: Test performance with https://www.eevblog.com/forum/microcontrollers/lightweight-sin(2x)-cos(2x)-for-mcus-with-float-hardware/ ??
     */
    inline float distanceAccurate(float fromLat, float fromLon, float toLat, float toLon)
    {
        fromLat *= DEG_TO_RADS;
        fromLon *= DEG_TO_RADS;
        toLat *= DEG_TO_RADS;
        toLon *= DEG_TO_RADS;
        float a = sinf(0.5f * (toLat - fromLat));
        float b = sinf(0.5f * (toLon - fromLon));
        float c = a * a + cosf(fromLat) * cosf(toLat) * b * b;
        float d = 2.f * atan2f(sqrtf(c), sqrtf(fabsf(1.f - c)));
        return d * DIAMETER_EARTH_M;
    }

    inline float wrapLonDelta(float dLon)
    {
        // Wrap to [-180, +180]
        if (dLon > 180.0f)
        {
            dLon -= 360.0f;
        }
        if (dLon < -180.0f)
        {
            dLon += 360.0f;
        }
        return dLon;
    }

    inline auto northEastDistance(float fromLat, float fromLon, float toLat, float toLon)
    {
        struct RelNorthRelEast
        {
            float north;
            float east;
        };

        float kx = cosf(fromLat * DEG_TO_RADS) * 111321.0f;
        float dx = wrapLonDelta(toLon - fromLon) * kx;
        float dy = (toLat - fromLat) * 111139.0f;
        return RelNorthRelEast{dy, dx};
    }

    /**
     * Calculate the distance between two points on earth fast but less accurate
     * For accurate values use distanceAccurate
     *
     * Note: Distance in what we are interested in <30Km at lat 30 degrees the difference is about 15m
     *
     * returns distance in meters
     * https://jamesloper.com/fastest-way-to-calculate-distance-between-two-coordinates
     * https://www.movable-type.co.uk/scripts/latlong.html
     */
    inline float distanceFast(float fromLat, float fromLon, float toLat, float toLon)
    {
        auto ne = northEastDistance(fromLat, fromLon, toLat, toLon);
        return sqrtf((ne.north * ne.north) + (ne.east * ne.east));
    }

    /**
     * Calculate the bearing between two points on earth
     * returns bearing in radians 0≤x<2π
     * For short distances you can also use bearingFromInDegShort
     * https://www.movable-type.co.uk/scripts/latlong.html
     */
    inline float bearingFromInRad(float fromLat, float fromLon, float toLat, float toLon)
    {
        fromLat *= DEG_TO_RADS;
        fromLon *= DEG_TO_RADS;
        toLat *= DEG_TO_RADS;
        toLon *= DEG_TO_RADS;
        float dLon = toLon - fromLon;
        float cosToLat = cosf(toLat);
        float bearingDegrees = atan2f(sinf(dLon) * cosToLat, (cosf(fromLat) * sinf(toLat)) - (sinf(fromLat) * cosToLat * cosf(dLon)));
        return fmodf((bearingDegrees + M_TWOPIF), M_TWOPIF);
    }

    /**
     * Same as bearingFrom but returns in degrees
     */
    inline float bearingFromInDeg(float fromLat, float fromLon, float toLat, float toLon)
    {
        return bearingFromInRad(fromLat, fromLon, toLat, toLon) * RADS_TO_DEG;
    }

    /**
     * A faster method than using bearingFromInRad that works with the relative coordinates we have to calculate anyways
     * This works for shorter distances well but cannot be used for long distances (>1000Km)
     */
    inline float __time_critical_func(bearingFromInDegShort)(float east, float north)
    {
        float theta = atan2f(east, north);
        float deg = theta * 180.f / M_PIF;
        if (deg < 0.f)
        {
            deg += 360.f;
        }
        return deg;
    }

    /**
     * Get the relative North, Eats and distance from a position to a position
     * Distance returned is distance inmeters and take in considation the altitude
     * All values returned are in meters
     * @param from Position where to calculate baring from
     * @param to Position where to calculate baring to
     * @param distance Distance in meters over ground
     */

    struct distanceRelNorthRelEastInt
    {
        uint32_t distance;
        int32_t relNorth;
        int32_t relEast;
        uint16_t bearing()
        {
            float bearing = bearingFromInDegShort(static_cast<float>(relEast), static_cast<float>(relNorth));
            uint16_t rounded = static_cast<uint16_t>(bearing + 0.5f);
            if (rounded >= 360)
            {
                rounded = 0;
            }
            return rounded;
        }
    };

    struct distanceRelNorthRelEastFloat
    {
        float distance;
        float relNorthMeter;
        float relEastMeter;
        float bearing()
        {
            return bearingFromInDegShort(relEastMeter, relNorthMeter);
        }
    };

    distanceRelNorthRelEastInt getDistanceRelNorthRelEastInt(float fromLat, float fromLon, float toLat, float toLon);
    distanceRelNorthRelEastFloat getDistanceRelNorthRelEastFloat(float fromLat, float fromLon, float toLat, float toLon);

    inline distanceRelNorthRelEastInt getDistanceRelNorthRelEastInt(const GATAS::AircraftPositionInfo &from, const GATAS::AircraftPositionInfo &to)
    {
        return getDistanceRelNorthRelEastInt(from.lat, from.lon, to.lat, to.lon);
    }

    inline distanceRelNorthRelEastInt getDistanceRelNorthRelEastInt(const GATAS::OwnshipPositionInfo &from, const GATAS::AircraftPositionInfo &to)
    {
        return getDistanceRelNorthRelEastInt(from.lat, from.lon, to.lat, to.lon);
    }

    /**
     * Calculate the bearing and ensures it's between 0..360
     */
    template <typename T>
    constexpr T toBearing(T angle)
    {
        while (angle < static_cast<T>(0))
        {
            angle += static_cast<T>(360);
        }

        while (angle >= static_cast<T>(360))
        {
            angle -= static_cast<T>(360);
        }

        return angle;
    }

    /**
     * Calculates the relative North and East + Bearing of two coordinates
     *
     * Note: This function takea around 75us..100us to complete
     */
    inline distanceRelNorthRelEastInt __time_critical_func(getDistanceRelNorthRelEastInt)(float fromLat, float fromLon, float toLat, float toLon)
    {
        auto drne = getDistanceRelNorthRelEastFloat(fromLat, fromLon, toLat, toLon);
        return {
            static_cast<uint32_t>(drne.distance + 0.5f),
            static_cast<int32_t>(drne.relNorthMeter + 0.5f),
            static_cast<int32_t>(drne.relEastMeter + 0.5f),
        };
    }

    /**
     * From two lat/lon points calculate relativeNorth/relativeEast distance and bearing between the two points
     */
    inline distanceRelNorthRelEastFloat __time_critical_func(getDistanceRelNorthRelEastFloat)(float fromLat, float fromLon, float toLat, float toLon)
    {
        auto ne = northEastDistance(fromLat, fromLon, toLat, toLon);
        // float bearing = bearingFromInDegShort(ne.east, ne.north);
        float distance = sqrtf((ne.north * ne.north) + (ne.east * ne.east));
        return {distance, ne.north, ne.east};
    }

    /**
     * Parse an path in the form of /foo/bar/bas.extension
     * returns a vector with foo, bar, bas, extension
     */
    const etl::vector<GATAS::Modulename, 7> parsePath(const etl::string_view path, const etl::string_view key = "");

    /**
     * Devide a circle in a number of sections.
     * The sections is devided such that radial zero falls from -XX to XX where XX = M_PI/sections, section 1 XX..2XX etc
     * The resultng value is always 0..(SECTIONS-1)
     */
    template <int SECTIONS>
    int getRadialSection(int16_t degree)
    {
        constexpr int16_t sectionSize = 360 / SECTIONS;
        degree = toBearing(degree);

        // Calculate the section
        return static_cast<int>(fmodf((degree + (sectionSize >> 1)) / sectionSize, SECTIONS));
    }

    template <int SECTIONS>
    int getRadialSectionRad(float rad)
    {
        return getRadialSection<SECTIONS>(rad * RADS_TO_DEG);
    }

    /*
     * taken from XCSoar
     * Get's the Geiod of a specific location, based on a simple lookup table
     * See also : https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
     * Note: Only use of the GPS with GGA does nit send the egm96 data
     */
    int8_t egmGeoidOffset(float lat, float lon);

    inline uint8_t calculateNMEAChecksum(const etl::istring &nmea)
    {
        uint8_t chk = 0;
        // NMEA checksum starts after '$'
        for (size_t i = 1; i < nmea.size() && nmea[i] != '*'; ++i)
        {
            chk ^= static_cast<uint8_t>(nmea[i]);
        }

        return chk;
    }

    /**
     * Add's the checksum and postfix characters to a NMEA string. It may contain an existing checksum that will be overwritten
     * When the capacity is not enough, the result is undefined
     * Note: Must start with the prefix character $
     * @param nmea example '$PFEC,GPint,RMC05'
     * @param appendCRLF append the NMEA line terminator when true
     * @return             '$PFEC,GPint,RMC05*2D\r\n' when appendCRLF is true
     */
    inline void addChecksumToNMEA(etl::istring &nmea, bool appendCRLF = true)
    {
        const char hexChars[] = "0123456789ABCDEF";

        const uint8_t chk = calculateNMEAChecksum(nmea);

        // Strip existing checksum if present
        const auto starPos = nmea.find('*');
        if (starPos != etl::string_view::npos)
        {
            nmea.resize(starPos);
        }

        if (nmea.capacity() - nmea.size() < 5) // Keep 5, just in case when /r/l needs to be appended again
        {
            return; // not enough space
        }

        char suffix[] = {
            '*',
            hexChars[(chk >> 4) & 0x0F],
            hexChars[chk & 0x0F],
            '\r',
            '\n'};

        const size_t suffixSize = appendCRLF ? 5 : 3;
        nmea.append(suffix, suffixSize);
    }

    inline bool validateNMEAChecksum(const etl::istring &nmea)
    {
        const auto starPos = nmea.find('*');
        if (starPos == etl::string_view::npos || starPos + 2 >= nmea.size())
        {
            return false;
        }

        auto hexToNibble = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            return 0xFF;
        };

        const uint8_t hi = hexToNibble(nmea[starPos + 1]);
        const uint8_t lo = hexToNibble(nmea[starPos + 2]);

        if (hi == 0xFF || lo == 0xFF)
        {
            return false;
        }

        const uint8_t expected = calculateNMEAChecksum(nmea);

        return expected == ((hi << 4) | lo);
    }

    template <size_t Array_Size>
    etl::string<Array_Size + 6> createNmeaChecksum(const char (&text)[Array_Size])
    {
        etl::string<Array_Size + 6> nmea(text, etl::strlen(text, Array_Size - 1));
        addChecksumToNMEA(nmea);
        return nmea;
    }

    size_t getTotalHeap(void);
    size_t getFreeHeap(void);

    /**
     * Create an textual representation of the aircraftId. FOr the moment it will simply turn the aircraftID as received into a textual HEX representation
     * Later the idea is that it will use DDB to get the registration based on aircraftID and addressType
     *
     */
    inline void streamIcaoAddress(etl::string_stream &stream, uint32_t aircraftID, GATAS::AddressType addressType, etl::string_view callSign = "")
    {
        (void)addressType;
        stream << etl::hex << etl::setw(6) << etl::setfill('0') << etl::uppercase << aircraftID << GATAS::RESET_FORMAT;
        if (callSign.size() > 0)
        {
            stream << "!" << callSign;
        }
    }

    inline uint8_t getHexVal(char hex)
    {
        uint8_t val = (uint8_t)hex;
        // For uppercase A-F letters:
        // return val - (val < 58 ? 48 : 55);
        // For lowercase a-f letters:
        // return val - (val < 58 ? 48 : 87);
        // Or the two combined, but a bit slower:
        return val - (val < 58 ? 48 : (val < 97 ? 55 : 87));
    }

    /**
     * Convert a hex string in the form of 0123FA... to a byte array
     */
    inline void hexStrToByteArray(const char hex[], uint8_t hexLength, uint8_t bytearray[])
    {
        for (uint8_t i = 0, j = 0; i < hexLength; i += 2, ++j)
        {
            bytearray[j] = (getHexVal(hex[i]) << 4) | getHexVal(hex[i + 1]);
        }
    }

    /**
     * Convert a HEX string to a byte array to a maximum length of 254 characters/
     */
    [[maybe_unused]] static void hexStrToByteArray(const char *hex, uint8_t byteArray[])
    {
        auto hexLength = strlen(hex);
        if (hexLength > 254)
        {
            return;
        }
        hexStrToByteArray(hex, static_cast<uint8_t>(hexLength), byteArray);
    }

    /**
     * Convert a byteArray to a hex string, the reverse of hexStrToByteArray
     */
    inline void byteArrayToHexStr(const uint8_t byteArray[], uint8_t byteArrayLength, char hexStr[])
    {
        const char hexChars[] = "0123456789ABCDEF";
        for (uint8_t i = 0; i < byteArrayLength; ++i)
        {
            hexStr[i * 2] = hexChars[(byteArray[i] >> 4) & 0x0F]; // Extract the upper 4 bits
            hexStr[i * 2 + 1] = hexChars[byteArray[i] & 0x0F];    // Extract the lower 4 bits
        }
        hexStr[byteArrayLength * 2] = '\0'; // Null-terminate the string
    }

    /**
     * Returns the pin number from the pin map, when not found returns -1 to indicate that
     */
    inline uint8_t pinValue(const GATAS::PinTypeMap &pm, const GATAS::PinType &pinName, uint8_t defaultValue = UINT8_MAX)
    {
        auto it = pm.find(pinName);
        if (it != pm.end())
        {
            return it->second;
        }
        return defaultValue;
    }

    /**
     * Decide if an aircraft is airborn based on category and speed
     * Own our ownship is a Helicopter or baloon, it's best to indicate we are airborn, even if we don't have an airspeed
     * This is the same for obstacle or slow moving objects. Some EFB's don't show traffic on 'ground', so to ensure
     * they are indicated, airborn will be set to true
     */
    inline bool isAirborn(GATAS::AircraftCategory cat, float speedMs)
    {
        switch (cat)
        {
        case GATAS::AircraftCategory::LIGHT_THAN_AIR:
        case GATAS::AircraftCategory::SKY_DIVER:
        case GATAS::AircraftCategory::ROTORCRAFT:
        case GATAS::AircraftCategory::UNKNOWN:
        case GATAS::AircraftCategory::UN_MANNED:
        case GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE:
        case GATAS::AircraftCategory::SURFACE_VEHICLE:
        case GATAS::AircraftCategory::POINT_OBSTACLE:
        case GATAS::AircraftCategory::CLUSTER_OBSTACLE:
        case GATAS::AircraftCategory::LINE_OBSTACLE:
        case GATAS::AircraftCategory::HANG_GLIDER:
        case GATAS::AircraftCategory::PARA_GLIDER:
            return true;
        default:
            return speedMs > GATAS::GROUNDSPEED_CONSIDERING_AIRBORN; // 10 m/s threshold for others
        }
    }

    /**
     * Find a pin number and if not found, return the default
     * @param pinMap
     * @param pin
     * @param defaultNum
     */
    inline uint8_t getPin(const GATAS::PinTypeMap &pinMap, GATAS::PinType pin, uint8_t defaultNum)
    {
        auto it = pinMap.find(pin);
        if (it != pinMap.end())
        {
            return static_cast<uint8_t>(it->second);
        }
        return defaultNum;
    }
}
