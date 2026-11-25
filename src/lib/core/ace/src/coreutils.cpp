#include "../coreutils.hpp"
#include "../egm2008_dem.hpp"

#include "etl/string_utilities.h"

#if defined(__MACH__)
#include "pico/stdlib.h"
#else
#include <malloc.h>
#endif

namespace
{
    // Packet timestamps carry a millisecond offset inside the current minute.
    // Received aircraft positions were sent before or at the receive time, so
    // the packet time must resolve to now or the previous minute. A numerically
    // larger millisecond value is therefore previous-minute rollover, not a
    // future timestamp: now xx:xx:00.000 and packet 59'000 returns -1000.
    int32_t minuteMsDelta(uint16_t msInMinute, uint64_t nowMsSinceEpoch)
    {
        const int32_t currentMsInMinute = static_cast<int32_t>(nowMsSinceEpoch % 60'000ULL);
        int32_t deltaMs = static_cast<int32_t>(msInMinute) - currentMsInMinute;

        if (deltaMs > 0)
        {
            deltaMs -= 60'000;
        }

        return deltaMs;
    }

    // Time Keeping
    uint64_t offsetTimeToAbsolute = 0;
    int32_t timeUs32PpsOffset = 0;
    bool epochTimeValid = false;
    spin_lock_t *claimedSpinLock = nullptr;
}

namespace CoreUtils
{
void init()
{
    claimedSpinLock = SpinlockGuard::claim();
}

spin_lock_t *sharedSpinLock()
{
    return claimedSpinLock;
}

bool isUsReached(uint32_t referenceUs, uint32_t us)
{
    return usToReference(referenceUs, us) <= 0;
}

uint32_t timeUs32()
{
    return time_us_32() - timeUs32PpsOffset;
}

uint64_t timeUs64()
{
    // time_us_64 and time_us_32 use the same hardware time, thus offset is also the same
    return time_us_64() - timeUs32PpsOffset;
}

void __time_critical_func(setPPS)(int32_t offsetUs)
{
    timeUs32PpsOffset = time_us_32() % 1'000'000 - offsetUs;
}

void setOffsetMsSinceEpoch(uint64_t msSinceEpoch)
{
    const uint64_t nowUsRaw = time_us_64();
    offsetTimeToAbsolute = msSinceEpoch - nowUsRaw / 1'000;
    epochTimeValid = true;
}

uint64_t msSinceEpoch()
{
    return (time_us_64() / 1'000) + offsetTimeToAbsolute;
}

etl::optional<uint32_t> timeUs32FromMsInMinute(uint16_t msInMinute, uint16_t maxAbsDeltaMs)
{
    GATAS_ASSERT(msInMinute < 60'000, "msInMinute must be < 60000");
    if (!epochTimeValid)
    {
        return etl::nullopt;
    }

    const int32_t deltaMs = minuteMsDelta(msInMinute, msSinceEpoch());

    if (deltaMs < -static_cast<int32_t>(maxAbsDeltaMs))
    {
        return etl::nullopt;
    }

    const uint32_t nowUs = timeUs32();
    const uint32_t currentMsBoundaryUs = nowUs - (nowUs % 1'000UL);
    const uint32_t deltaUs = static_cast<uint32_t>(deltaMs * 1'000);
    return currentMsBoundaryUs + deltaUs;
}

const etl::vector<GATAS::Modulename, 7> parsePath(const etl::string_view path, const etl::string_view key)
{
    GATAS::ConfigString totalPath(path);
    if (key.size())
    {
        totalPath.append("/");
        totalPath.append(key);
    }
    using StringView = etl::string_view;
    using Vector = etl::vector<GATAS::Modulename, 7>;
    using Token = etl::optional<StringView>;

    Vector tokens;
    Token token;
    while ((token = etl::get_token(totalPath, "/.?&", token, true)))
    {
        if (tokens.full())
        {
            break;
        }
        tokens.emplace_back(token.value());
    }
    return tokens;
}

size_t getTotalHeap(void)
{
#if defined(PICO_RP2040) || defined(PICO_RP2350)
    extern char __StackLimit, __bss_end__;
    return static_cast<size_t>(
        reinterpret_cast<uintptr_t>(&__StackLimit) -
        reinterpret_cast<uintptr_t>(&__bss_end__));
#else
    return 0;
#endif
}

size_t getFreeHeap(void)
{
// We hit this during unit testing, we return 0 because it would
// properly be useless
#if !defined(__arm__)
    return 0;
#else
    struct mallinfo m = mallinfo();
    return getTotalHeap() - m.uordblks;
#endif
}

int8_t egmGeoidOffset(float lat, float lon)
{
    // Convert directly to index space
    constexpr float invRes = 1.0f / egm2008_resolution_deg;

    const int lat_idx = static_cast<int>((egm2008_max_lat - lat) * invRes + 0.5f);
    const int lon_idx = static_cast<int>((lon - egm2008_min_lon) * invRes + 0.5f);

    // Single bounds check (fast path)
    if ((unsigned)lat_idx >= egm2008_lat_steps || (unsigned)lon_idx >= egm2008_lon_steps)
    {
        return 0;
    }

    return egm2008s_dem[lon_idx][lat_idx];
}
}
