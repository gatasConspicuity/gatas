#include "pico/cyw43_arch.h"

#if GATAS_DEBUG == 1
static bool tcpip_thread_started;

void gatas_lwip_mark_tcpip_thread(void)
{
    tcpip_thread_started = true;
}

void gatas_lwip_assert_core_locked(void)
{
    // tcpip_init() initializes lwIP before starting its thread or taking the
    // core lock. Match lwIP's usual startup exemption, then check every call.
    if (tcpip_thread_started)
    {
        cyw43_arch_lwip_check();
    }
}
#endif
