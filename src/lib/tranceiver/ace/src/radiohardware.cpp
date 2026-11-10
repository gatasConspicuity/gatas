#include "../radiohardware.hpp"

bool RadioHardware::waitReady()
{
    const auto start = CoreUtils::timeUs32Raw();
    auto nextYield = start + 3000;
    while (gpio_get(busyPin))
    {
        const auto now = CoreUtils::timeUs32Raw();
        if (CoreUtils::isUsReachedRaw(start + 25000, now))
        {
            recordHalBusyTimeout();
            return false;
        }
        if (CoreUtils::isUsReachedRaw(nextYield, now))
        {
            nextYield = now + 3000;
            if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) { taskYIELD(); }
        }
    }
    return true;
}

bool RadioHardware::write(const uint8_t *data, uint16_t length)
{
    if (length == 0) { return true; }
    if (spi_write_blocking(spiHall->spiNum() ? spi1 : spi0, data, length) != length)
    {
        recordHalSpiError();
        return false;
    }
    return true;
}

bool RadioHardware::read(uint8_t *data, uint16_t length)
{
    if (length == 0) { return true; }
    // LR20xx requires NOP (zero) on MOSI while reading.
    if (spi_read_blocking(spiHall->spiNum() ? spi1 : spi0, 0, data, length) != length)
    {
        recordHalSpiError();
        return false;
    }
    return true;
}

bool RadioHardware::reset()
{
    // Only a dedicated per-radio RST is owned here. Shared reset belongs to AceSpi.
    if (resetPin == UINT8_MAX) { recordHalSpiError(); return false; }
    gpio_init(resetPin);
    gpio_put(resetPin, 0);
    gpio_set_dir(resetPin, GPIO_OUT);
    busy_wait_us_32(200);
    gpio_put(resetPin, 1);
    busy_wait_us_32(1000);
    return waitReady();
}

bool RadioHardware::wakeup()
{
    spiHall->cs_select(csPin);
    busy_wait_us_32(100);
    spiHall->cs_deselect(csPin);
    busy_wait_us_32(1000);
    return waitReady();
}
