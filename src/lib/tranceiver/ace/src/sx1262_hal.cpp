#include "../../sx1262driver/src/sx126x_hal.h"

#include "../sx1262.hpp"

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"

/* PICO. */
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* GATAS. */
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"

/**
 * Wait for the BUSY pin to reach the requested state, yielding at most once
 * per yieldIntervalUs after the initial spin period.
 * Returns 0 on success and 1 on timeout.
 */
uint8_t sx126x_buzy_wait(uint8_t busyPin, uint8_t code = 0, bool waitFor = false)
{
    constexpr uint32_t timeoutUs = 25'000;
    constexpr uint32_t yieldIntervalUs = 3'000;
    (void)code;
    const uint32_t start = CoreUtils::timeUs32Raw();
    uint32_t nextYieldAt = start + yieldIntervalUs;

    while (gpio_get(busyPin) != waitFor)
    {
        const uint32_t now = CoreUtils::timeUs32Raw();

        if (CoreUtils::isUsReachedRaw(start + timeoutUs, now))
        {
            return 1;
        }

        if (!CoreUtils::isUsReachedRaw(nextYieldAt, now))
        {
            __asm volatile("nop");
            continue;
        }

        // Advance before yielding: taskYIELD() can return immediately, so
        // leaving the deadline in the past would yield on every poll.
        nextYieldAt = now + yieldIntervalUs;
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            taskYIELD();
        }
        else
        {
            __asm volatile("nop");
        }
    }

    return 0;
}

sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *command, const uint16_t command_length,
                                     const uint8_t *data, const uint16_t data_length)
{
    Sx1262 *sx1262 = (Sx1262 *)context;
    SpiModule *spi = sx1262->spi();

    sx126x_hal_status_t ret = SX126X_HAL_STATUS_OK;
    if (sx126x_buzy_wait(sx1262->busy()))
    {
        sx1262->recordHalBusyTimeout();
        GATAS_WARN("hal write, Wait busy timeout opcode:0x%02x busy:%d", command[0], sx1262->busy());
        ret = SX126X_HAL_STATUS_ERROR;
    }
    else
    {
        spi->cs_select(sx1262->cs());
        if (spi_write_blocking(spi->spiNum() ? spi1 : spi0, command, command_length) != command_length)
        {
            sx1262->recordHalSpiError();
            GATAS_WARN("hal write, SPI command error opcode:0x%02x", command[0]);
            ret = SX126X_HAL_STATUS_ERROR;
        }
        else if (data_length != 0 &&
                 spi_write_blocking(spi->spiNum() ? spi1 : spi0, data, data_length) != data_length)
        {
            sx1262->recordHalSpiError();
            GATAS_WARN("hal write, SPI data error opcode:0x%02x", command[0]);
            ret = SX126X_HAL_STATUS_ERROR;
        }
    }
    spi->cs_deselect(sx1262->cs());

    // BUSY can assert up to 600 ns after NSS rises. Cover that window before
    // Delay to wait for a minimal Tsw
    busy_wait_us_32(1);

    return ret;
}

sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command, const uint16_t command_length,
                                    uint8_t *data, const uint16_t data_length)
{
    Sx1262 *sx1262 = (Sx1262 *)context;
    SpiModule *spi = sx1262->spi();

    sx126x_hal_status_t ret = SX126X_HAL_STATUS_OK;
    if (sx126x_buzy_wait(sx1262->busy()))
    {
        sx1262->recordHalBusyTimeout();
        GATAS_WARN("hal read, Wait busy timeout opcode:0x%02x", command[0]);
        ret = SX126X_HAL_STATUS_ERROR;
    }
    else
    {
        spi->cs_select(sx1262->cs());
        int length = spi_write_blocking(spi->spiNum() ? spi1 : spi0, command, command_length);
        if (length != command_length)
        {
            sx1262->recordHalSpiError();
            GATAS_WARN("hal read, SPI command error opcode:0x%02x", command[0]);
            ret = SX126X_HAL_STATUS_ERROR;
        }
        else
        {
            length = spi_read_blocking(spi->spiNum() ? spi1 : spi0, 0, data, data_length);
            if (length != data_length)
            {
                sx1262->recordHalSpiError();
                GATAS_WARN("hal read, SPI data error opcode:0x%02x", command[0]);
                ret = SX126X_HAL_STATUS_ERROR;
            }
        }
    }
    spi->cs_deselect(sx1262->cs());
    return ret;
}

sx126x_hal_status_t sx126x_hal_reset(const void *context)
{
    // Reset is already given once when the SPI starts up
    (void)context;
    GATAS_INFO("SX1262 Reset called");
    return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_wakeup(const void *context)
{
    (void)context;
    GATAS_INFO("SX1262 wakeup called");
    return SX126X_HAL_STATUS_OK;
}
