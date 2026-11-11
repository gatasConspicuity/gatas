#pragma once

/* System. */
#include <stdint.h>

/* FreeRTOS. */
#include "FreeRTOS.h"

/* PICO. */
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* GATAS. */
#include "ace/coreutils.hpp"
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/semaphoreguard.hpp"

#ifndef GATAS_SPI_DEFAULT_BUS_FREQUENCY
#define GATAS_SPI_DEFAULT_BUS_FREQUENCY (15)
#endif

/**
 * Class that is responsible for managing an SPI bus between various devices.
 */
class AceSpi : public SpiModule, public etl::message_router<AceSpi>
{
private:
    /**
     * Reset all attached devices that is using the rst pin
     */
    void resetDevices() const;

private:
    static constexpr uint8_t READ_BIT = 0x80;
    const uint8_t clk;
    const uint8_t mosi;
    const uint8_t miso;
    const uint8_t rst;
    const uint8_t spi;
    uint8_t lastBusFrequency;
    SemaphoreHandle_t mutex;
public:
    static constexpr const etl::string_view NAME = "AceSpi";
    static constexpr uint8_t MAX_SPI_MODULES = SpiModule::MAX_SPI_MODULES;
    static constexpr etl::array<etl::string_view, MAX_SPI_MODULES> NAMES{"AceSpi_0", "AceSpi_1"};

    // Only fallback to AceSpi for AceSpi_0
    static const GATAS::PinTypeMap pinMap(const Configuration &config, uint8_t device)
    {
        return config.pinMap(NAMES[device], device == 0 ? NAME : etl::string_view());
    }

    AceSpi(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t device) : SpiModule(bus, SpiModule::NAMES[device]),
                                                                                   clk(pins.at(GATAS::PinType::CLK)),
                                                                                   mosi(pins.at(GATAS::PinType::MOSI)),
                                                                                   miso(pins.at(GATAS::PinType::MISO)),
                                                                                   rst(CoreUtils::getPin(pins, GATAS::PinType::RST, UINT8_MAX)),
                                                                                   spi(pins.at(GATAS::PinType::SPI)),
                                                                                   lastBusFrequency(GATAS_SPI_DEFAULT_BUS_FREQUENCY)
    {
    }

    AceSpi(etl::imessage_bus &bus, const Configuration &config, uint8_t device) : AceSpi(bus, pinMap(config, device), device)
    {
    }

    // Safe only after failed postConstruct(), before start().
    // Failure precedes SPI setup and leaves no allocated mutex.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~AceSpi() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void read_registers(uint8_t cs, uint8_t reg, uint8_t *buf, uint16_t len, uint8_t delayMs) const override;

    virtual void read_registers_select(uint8_t cs, uint8_t reg) const override;

    virtual void read_registers_read(uint8_t cs, uint8_t *buf, uint16_t len) const override;

    virtual void cs_select(uint8_t cs) const override;

    virtual void cs_deselect(uint8_t cs) const override;

    virtual void write_array(uint8_t cs, uint8_t *data, uint8_t length, uint8_t delayMs) const override;

    virtual void write_byte(uint8_t cs, uint8_t data, uint8_t delayMs) const override;

    virtual SpiGuard getLock(bool &locked) override;

    virtual uint8_t spiNum() const override
    {
        return spi;
    }

    void on_receive_unknown(const etl::imessage &msg);
};
