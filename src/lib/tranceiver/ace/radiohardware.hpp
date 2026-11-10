#pragma once
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"
#include "hardware/spi.h"
#include "pico/stdlib.h"

// Platform transport shared by the chip-specific HALs. The caller owns the SPI
// lock for the complete operation, including both phases of an LR20xx read.
class RadioHardware
{
protected:
    const uint8_t csPin;
    const uint8_t busyPin;
    const uint8_t dio1Pin; // MCU interrupt pin; LR2021 may route DIO7 here.
    const uint8_t resetPin;
    SpiModule *spiHall = nullptr;
    bool halError = false;
    uint32_t halBusyTimeout = 0;
    uint32_t halSpiErrors = 0;
public:
    explicit RadioHardware(const GATAS::PinTypeMap &pins)
        : csPin(CoreUtils::getPin(pins, GATAS::PinType::CS, UINT8_MAX)), busyPin(CoreUtils::getPin(pins, GATAS::PinType::BUSY, UINT8_MAX)),
          dio1Pin(CoreUtils::getPin(pins, GATAS::PinType::DIO1, UINT8_MAX)), resetPin(CoreUtils::getPin(pins, GATAS::PinType::RST, UINT8_MAX)) {}
    uint8_t cs() const { return csPin; }
    uint8_t busy() const { return busyPin; }
    SpiModule *spi() { return spiHall; }
    void recordHalBusyTimeout() { ++halBusyTimeout; halError = true; }
    void recordHalSpiError() { ++halSpiErrors; halError = true; }
    void clearHalError() { halError = false; }
    bool hasHalError() const { return halError; }
    bool waitReady();
    bool write(const uint8_t *data, uint16_t length);
    bool read(uint8_t *data, uint16_t length);
    bool reset();
    bool wakeup();
};
