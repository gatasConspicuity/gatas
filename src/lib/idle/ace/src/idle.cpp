#include "../idle.hpp"
#include "ace/coreutils.hpp"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

void Idle::start()
{
    if (xTaskCreate(idleTask, Idle::NAME.cbegin(), configMINIMAL_STACK_SIZE + 1024, this, tskIDLE_PRIORITY + 6, &taskHandle) != pdPASS)
    {
        cancel_repeating_timer(&timer);
        BaseModule::setModuleStatus(NAME, GATAS::PostConstruct::TASK_ERROR);
        return;
    }

    getBus().subscribe(*this);
};

void Idle::on_receive(const GATAS::GpsStatsMsg &msg)
{
    hasGpsFix = msg.gpsStats.gpsFix.hasFix;
    calculatePattern();
}

void Idle::on_receive(const GATAS::WifiConnectionStateMsg &wcs)
{
    wifiMode = wcs.wifiMode;
    calculatePattern();
}

void Idle::on_receive(const GATAS::SystemActionMsg &msg)
{
    performSystemAction(msg.action);
}

void Idle::performSystemAction(GATAS::DeviceAction action)
{
    switch (action)
    {
    case GATAS::DeviceAction::Restart:
        watchdog_reboot(0, 0, 0);
        break;
    case GATAS::DeviceAction::UsbBoot:
        reset_usb_boot(0, 0);
        break;
    default:
        return;
    }

    while (true)
    {
        tight_loop_contents();
    }
}

void Idle::calculatePattern()
{
    uint8_t wifiModePattern;
    switch (wifiMode)
    {
    case GATAS::WifiMode::AP:
        wifiModePattern = WIFI_AP_PATTERN;
        break;
    case GATAS::WifiMode::CLIENT:
        wifiModePattern = WIFI_CLIENT_PATTERN;
        break;
    default:
        wifiModePattern = WIFI_NC_PATTERN;
    }

    blinkPattern = hasGpsFix ? wifiModePattern << 16 | wifiModePattern << 8 | wifiModePattern : wifiModePattern;
}

void Idle::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void Idle::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{}";
}

GATAS::PostConstruct Idle::postConstruct()
{
    if (!add_repeating_timer_ms(1000 / 8, Idle::blinkCb, this, &timer))
    {
        return GATAS::PostConstruct::TIMER_ERROR;
    }
    return GATAS::PostConstruct::OK;
}

bool Idle::blinkCb(repeating_timer_t *t)
{
    auto idle = static_cast<Idle *>(t->user_data);
    if (idle->patternStep == 0)
    {
        idle->runningPattern = idle->blinkPattern;
        idle->patternStep = Idle::PATTERN_STEPS;
    }
    idle->patternStep--;
    gpio_put(idle->ledStatusIndicatorPin, idle->runningPattern & 0b1);

    idle->runningPattern >>= 1;
    return true;
}

void Idle::idleTask(void *arg)
{
    (void)arg;
    Idle *at = static_cast<Idle *>(arg);
    uint8_t msgFlags = 0;
    constexpr uint8_t DO_5S = 1 << 0;
    constexpr uint8_t DO_15S = 1 << 1;
    constexpr uint8_t DO_30S = 1 << 2;
    constexpr uint8_t DO_300S = 1 << 3;

#if GATAS_DEBUG != 1
    watchdog_enable(3500, 0);
#endif
    while (true)
    {
#if GATAS_DEBUG != 1
        watchdog_update();
#endif
        uint32_t tick = CoreUtils::secondsSinceEpoch();

        at->getBus().receive(GATAS::Every1SecMsg());
// Perhaps it's better for now to leave this flashing to indicate that FLASH worked
// And that the PICO is actually doing something
// #if GATAS_DEBUG == 1
        // Only flash via cy43 in DEBUG (I don't have  a led on my debug device)
        // to reduce resources for a release version
        // Real status with the led because that is light weight
        if (cyw43_arch_async_context() != nullptr)
        {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            vTaskDelay(TASK_DELAY_MS(100));
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }
// #endif
        if (tick % 5 == 0)
        {
            msgFlags |= DO_5S;
        }
        if (tick % 15 == 0)
        {
            msgFlags |= DO_15S;
        }
        if (tick % 30 == 0)
        {
            msgFlags |= DO_30S;
        }
        if (tick % 300 == 0)
        {
            msgFlags |= DO_300S;
        }

        if (msgFlags & DO_300S)
        {
            at->getBus().receive(GATAS::Every300SecMsg());
            msgFlags &= ~DO_300S;
        }
        else if (msgFlags & DO_30S)
        {
            at->getBus().receive(GATAS::Every30SecMsg());
            msgFlags &= ~DO_30S;
        }
        else if (msgFlags & DO_15S)
        {

#if GATAS_DEBUG == 1 && LWIP_STATS == 1 && MEMP_STATS == 1 && LWIP_STATS_DISPLAY
            puts("\033[2J\033[H\n\nLWiP Status:");
            for (int i = 0; i < MEMP_MAX; i++)
            {
                const struct memp_desc *desc = memp_pools[i];
                if (desc == NULL)
                    continue;

                struct stats_mem *stats = desc->stats;
                printf("Pool %-20s | avail: %3u | used: %3u | max: %3u | err: %3u\n",
                       desc->desc,
                       (unsigned int)(stats->avail),
                       (unsigned int)(stats->used),
                       (unsigned int)(stats->max),
                       (unsigned int)(stats->err));
            }
#endif
            at->getBus().receive(GATAS::Every15SecMsg());
            msgFlags &= ~DO_15S;
        }
        else if (msgFlags & DO_5S)
        {
            at->getBus().receive(GATAS::Every5SecMsg());
            msgFlags &= ~DO_5S;
        }
        else
        {
            at->getBus().receive(GATAS::IdleMsg());
        }

        vTaskDelay(TASK_DELAY_MS(CoreUtils::msDelayToReference(0)));
    }
}
