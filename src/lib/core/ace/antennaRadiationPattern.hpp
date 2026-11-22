#pragma once

#include "coreutils.hpp"
#include "messages.hpp"
#include "etl/array.h"

namespace GATAS
{
    /**
     * @brief Helper class to store antenna radiation pattern measurements.
     *
     * @tparam NUM_RADIALS
     */
    template <size_t NUM_RADIALS = 8>
    class AntennaRadiationPattern
    {
    public:
        struct Measurement
        {
            int16_t avgRssiDbm;
            int16_t maxRssiDbm;
            uint32_t avgDistance;
            uint32_t maxDistance;

            Measurement() : avgRssiDbm(-128), maxRssiDbm(-128), avgDistance(0), maxDistance(0)
            {
            }

            Measurement(const Measurement &) = delete;
            Measurement &operator=(const Measurement &) = delete;
            Measurement(Measurement &&) = delete;
            Measurement &operator=(Measurement &&) = delete;
        };

    private:
        etl::array<Measurement, NUM_RADIALS> radiationPattern;

    public:
        AntennaRadiationPattern() : radiationPattern() {};

        static float calculateRelativeBearing(int32_t relNorthFromOwn, int32_t relEastFromOwn, float ownTrack)
        {
            const float bearingFromOwn = CoreUtils::bearingFromInDegShort(static_cast<float>(relEastFromOwn), static_cast<float>(relNorthFromOwn));
            return CoreUtils::toBearing(bearingFromOwn - ownTrack);
        }

        void put(const GATAS::IngressAircraftPositionMsg &msg, float ownshipLat, float ownshipLon, float ownTrack)
        {
            put(msg.position, msg.rssidBm, ownshipLat, ownshipLon, ownTrack);
        }

        void put(const GATAS::AircraftPositionInfo &position, int16_t rssiDbm,
                 float ownshipLat, float ownshipLon, float ownTrack)
        {
            auto relativeFromOwn = position.relativeFromOwn(ownshipLat, ownshipLon);
            const float relativeBearing = calculateRelativeBearing(relativeFromOwn.relNorth, relativeFromOwn.relEast, ownTrack);
            uint8_t positionInRadial = static_cast<uint8_t>(CoreUtils::getRadialSection<NUM_RADIALS>(static_cast<int16_t>(relativeBearing)));
            Measurement &measurement = radiationPattern[positionInRadial];

            if (position.distanceFromOwn > measurement.maxDistance)
            {
                measurement.maxDistance = position.distanceFromOwn;
                measurement.maxRssiDbm = rssiDbm == INT16_MIN ? -128 : rssiDbm;
            }
            measurement.avgDistance = (measurement.avgDistance + position.distanceFromOwn) / 2;
            if (rssiDbm != INT16_MIN)
            {
                measurement.avgRssiDbm = (measurement.avgRssiDbm + rssiDbm) / 2;
            }
        }

        const etl::array<Measurement, NUM_RADIALS>& _radiationPattern() const
        {
            return radiationPattern;
        }

        void serialize(etl::string_stream &stream) const
        {
            // Generates: [ [avgRssiDbm, maxRssiDbm, avgDistance, maxDistance], [avgRssiDbm, maxRssiDbm, avgDistance, maxDistance], ....]
            stream << "[";
            for (size_t i = 0; i < NUM_RADIALS; ++i)
            {
                const Measurement &m = radiationPattern[i];

                stream << "[" << m.avgRssiDbm << ","
                       << m.maxRssiDbm << ","
                       << m.avgDistance << ","
                       << m.maxDistance << "]";

                if (i < NUM_RADIALS - 1)
                {
                    stream << ",";
                }
            }

            stream << "]";
        }
    };

}
