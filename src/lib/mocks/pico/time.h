#pragma once

#include <stdint.h>
#include "../pico.h"

struct repeating_timer_t
{
    void *user_data = nullptr;
};

inline bool add_repeating_timer_ms(int32_t, bool (*)(repeating_timer_t *), void *userData, repeating_timer_t *timer)
{
    timer->user_data = userData;
    return true;
}

inline bool cancel_repeating_timer(repeating_timer_t *)
{
    return true;
}

inline uint64_t get_absolute_timeValue = 0;
// us since boot
inline uint64_t get_absolute_time()
{
    return get_absolute_timeValue;
}

inline uint64_t to_us_since_boot(uint64_t time)
{
    return get_absolute_time();
}

inline uint64_t to_ms_since_boot(uint64_t time)
{
    return get_absolute_time() / 1'000;
}

inline uint64_t time_us_Value = 0;
inline void time_us_64_SET(uint64_t v) 
{
    time_us_Value = v;
}

inline uint64_t time_us_64()
{
    return time_us_Value;
}

inline uint32_t time_us_32()
{
    return static_cast<uint32_t>(time_us_Value);
}
