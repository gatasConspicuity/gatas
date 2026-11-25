#pragma once

#include <cmath>

#include "etl/array.h"
#include "etl/optional.h"

#include "models.hpp"
#include "spinlockguard.hpp"
#include "pico/time.h"

// Forward declaration avoids the coreutils/messages/ownshipstate include cycle.
namespace CoreUtils
{
    uint64_t msSinceEpoch();
    uint32_t timeUs32();
    bool isUsReached(uint32_t referenceUs, uint32_t us);
}

namespace GATAS
{
    struct BarometricPressureSample
    {
        BarometricPressure value{};
        uint64_t msSinceEpoch = 0;
        bool valid = false;
        float pressureAltitudeMetres = 0.0f;
    };

    // Local update metadata only; never part of the pressure model or wire format.
    enum class BarometricSource : uint8_t
    {
        Internal,
        External
    };

    struct SelectedPressureAltitude
    {
        float metres;
        BarometricSource origin;
        PressureSource method;
    };

    struct OwnshipPressureState
    {
        struct AdsbOffset
        {
            uint32_t observedSeconds;
            uint32_t icao;
            int16_t gnssMinusBaroMetres;
        };
        etl::optional<AdsbOffset> adsbOffset;
        static constexpr size_t METHOD_COUNT =
            static_cast<size_t>(PressureSource::ADSBDerived);
        static constexpr size_t ORIGIN_COUNT = 2;

        etl::array<etl::array<BarometricPressureSample, METHOD_COUNT>, ORIGIN_COUNT> samples{};

        BarometricPressureSample &sample(BarometricSource origin, PressureSource method)
        {
            return samples[static_cast<size_t>(origin)][static_cast<size_t>(method)];
        }

        const BarometricPressureSample &sample(BarometricSource origin, PressureSource method) const
        {
            return samples[static_cast<size_t>(origin)][static_cast<size_t>(method)];
        }
    };

    /**
     * Inject one instance into producers and consumers. Initialize before use.
     * Retain the latest sample per origin and method. Selection uses one coherent
     * pressure snapshot; location is a separate snapshot.
     */
    class OwnshipState
    {
        SynchronizedValue<OwnshipPressureState> pressure;
        static bool validPressure(float value)
        {
            return std::isfinite(value) && value >= 1.0f && value <= 1100.0f;
        }

        static uint64_t maxAgeMs(PressureSource source)
        {
            switch (source)
            {
            case PressureSource::PressureSensor:
                return 5'000;

            case PressureSource::Calculated:
                return 5'000;

            default:
                return 5'000;
            }
        }

        static bool fresh(
            bool valid,
            uint64_t timestamp,
            uint64_t now,
            PressureSource source)
        {
            // Reject future samples before subtraction, including after a clock correction.
            return valid &&
                   timestamp <= now &&
                   now - timestamp <= maxAgeMs(source);
        }

    public:
        SynchronizedValue<OwnshipPositionInfo> location;

        // ADS-B-derived offsets expire after 150 seconds.
        static constexpr uint32_t OWNSHIP_POSITION_MAX_AGE_US = 5'000'000;
        static constexpr uint32_t ADSB_MAX_HORIZONTAL_DISTANCE_METRES = 10'000;
        static constexpr int32_t ADSB_MAX_VERTICAL_DISTANCE_METRES = 750;
        // Whole seconds retain the offset for at least 150 seconds.
        static constexpr uint32_t ADSB_OFFSET_MAX_AGE_SECONDS = 150;

        static bool withinAdsbReferenceRange(uint32_t horizontalMetres, int64_t verticalMetres)
        {
            return horizontalMetres < ADSB_MAX_HORIZONTAL_DISTANCE_METRES &&
                   verticalMetres > -ADSB_MAX_VERTICAL_DISTANCE_METRES && verticalMetres < ADSB_MAX_VERTICAL_DISTANCE_METRES;
        }

        void updateAdsbAltitudeDifference(uint32_t icao, int16_t differenceMetres, uint32_t horizontalMetres,
                                          int64_t verticalMetres, uint32_t observedSeconds = static_cast<uint32_t>(time_us_64() / 1'000'000))
        {
            if (withinAdsbReferenceRange(horizontalMetres, verticalMetres))
            {
                pressure.update([&](OwnshipPressureState &state)
                {
                    const auto &reference = state.adsbOffset;
                    if (!reference || reference->icao == icao ||
                        observedSeconds - reference->observedSeconds > ADSB_OFFSET_MAX_AGE_SECONDS)
                    {
                        state.adsbOffset = OwnshipPressureState::AdsbOffset{observedSeconds, icao, differenceMetres};
                    }
                });
            }
        }

        void updateBarometricPressure(
            BarometricSource origin,
            const BarometricPressure &value,
            uint64_t msSinceEpoch = CoreUtils::msSinceEpoch())
        {
            GATAS_ASSERT(origin == BarometricSource::Internal || origin == BarometricSource::External,
                         "Invalid pressure origin");
            GATAS_ASSERT(static_cast<uint8_t>(value.source) <
                             static_cast<uint8_t>(PressureSource::ADSBDerived),
                         "Invalid pressure source");
            const bool valid = validPressure(value.pressurehPa);
            // Standard-pressure altitude, referenced to 1013.25 hPa. The tropospheric
            // approximation applies below approximately 11 km; QNH is not required.
            // Calculate outside the spinlock and only for valid pressure samples.
            const float altitudeMetres = valid
                ? 44330.0f * (1.0f - powf(value.pressurehPa / 1013.25f, 0.190295f))
                : 0.0f;
            const BarometricPressureSample sample{value, msSinceEpoch, valid, altitudeMetres};
            pressure.update(
                [&](OwnshipPressureState &state)
                {
                    state.sample(origin, value.source) = sample;
                });
        }

        OwnshipPressureState loadPressureState() const
        {
            return pressure.load();
        }

        // Return the best fresh pressure altitude and the origin/method selected with it.
        etl::optional<SelectedPressureAltitude> selectedPressureAltitude(
            uint64_t nowMsSinceEpoch = CoreUtils::msSinceEpoch()) const
        {
            const auto snapshot = pressure.load();

            // Prefer measured pressure from either origin over estimated sources.
            constexpr BarometricSource origins[] = {BarometricSource::Internal, BarometricSource::External};
            for (const auto origin : origins)
            {
                const auto &sample = snapshot.sample(origin, PressureSource::PressureSensor);
                if (fresh(sample.valid, sample.msSinceEpoch, nowMsSinceEpoch, PressureSource::PressureSensor))
                {
                    return SelectedPressureAltitude{sample.pressureAltitudeMetres, origin, PressureSource::PressureSensor};
                }
            }

            const auto &ref = snapshot.adsbOffset;
            const auto monotonicSeconds = static_cast<uint32_t>(time_us_64() / 1'000'000);
            if (ref && monotonicSeconds - ref->observedSeconds <= ADSB_OFFSET_MAX_AGE_SECONDS)
            {
                const auto own = location.load();
                if (own.timestamp && CoreUtils::isUsReached(own.timestamp, CoreUtils::timeUs32()) &&
                    !CoreUtils::isUsReached(own.timestamp + OWNSHIP_POSITION_MAX_AGE_US + 1, CoreUtils::timeUs32()))
                {
                    return SelectedPressureAltitude{static_cast<float>(own.ellipseHeight) - ref->gnssMinusBaroMetres,
                                                    BarometricSource::Internal, PressureSource::ADSBDerived};
                }
            }
            constexpr PressureSource methods[] = {PressureSource::Calculated, PressureSource::Unavailable};
            for (const auto method : methods)
            {
                for (const auto origin : origins)
                {
                    const auto &sample = snapshot.sample(origin, method);
                    if (fresh(sample.valid, sample.msSinceEpoch, nowMsSinceEpoch, method))
                    {
                        return SelectedPressureAltitude{sample.pressureAltitudeMetres, origin, method};
                    }
                }
            }
            return etl::nullopt;
        }

        // Returns pressure altitude in metres from the best fresh ambient pressure.
        etl::optional<float> calculatePressureAltitude(
            uint64_t nowMsSinceEpoch = CoreUtils::msSinceEpoch()) const
        {
            if (const auto selected = selectedPressureAltitude(nowMsSinceEpoch))
            {
                return selected->metres;
            }
            return etl::nullopt;
        }

        void init(spin_lock_t *lock)
        {
            location.init(lock);
            pressure.init(lock);
        }
    };
}
