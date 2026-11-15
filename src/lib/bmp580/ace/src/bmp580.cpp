#include "../bmp580.hpp"

#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "ace/coreutils.hpp"

namespace
{
    constexpr uint8_t CHIP_ID = 0x50;
    constexpr uint8_t ADDRESSES[] = {0x46, 0x47};
    constexpr uint8_t REG_CHIP_ID = 0x01;
    constexpr uint8_t REG_INT_SOURCE = 0x15;
    constexpr uint8_t REG_PRESS_DATA = 0x20;
    constexpr uint8_t REG_INT_STATUS = 0x27;
    constexpr uint8_t REG_STATUS = 0x28;
    constexpr uint8_t REG_OSR_CONFIG = 0x36;
    constexpr uint8_t REG_ODR_CONFIG = 0x37;
    constexpr uint8_t REG_OSR_EFF = 0x38;
    constexpr uint8_t REG_CMD = 0x7e;
    constexpr uint8_t SOFT_RESET = 0xb6;
    constexpr uint8_t RESET_COMPLETE = 0x10;
    constexpr uint8_t NVM_READY = 0x02;
    constexpr uint8_t NVM_ERROR = 0x04;
    constexpr uint8_t DATA_READY = 0x01;
    constexpr uint8_t ODR_VALID = 0x80;
    constexpr uint8_t OVERSAMPLING = 0x40 | (4 << 3) | 1; // Pressure enabled, 16x pressure, 2x temperature.
    constexpr uint8_t STANDBY_5_HZ = 0x80 | (0x18 << 2); // Deep standby disabled, 5 Hz.
    constexpr uint8_t NORMAL_5_HZ = STANDBY_5_HZ | 1;
    constexpr uint32_t I2C_TIMEOUT_US = 2000;
}

Bmp580::Bmp580(etl::imessage_bus &bus, const Configuration &config, GATAS::OwnshipState &state)
    : BaseModule(bus, NAME),
      sda(CoreUtils::getPin(config.pinMap(NAME), GATAS::PinType::SDA, 0xff)),
      scl(CoreUtils::getPin(config.pinMap(NAME), GATAS::PinType::SCL, 0xff)),
      ownshipState(state)
{
}

template <size_t Size>
bool Bmp580::readRegisters(uint8_t reg, uint8_t (&data)[Size]) const
{
    return i2c_write_timeout_us(i2c, address, &reg, 1, true, I2C_TIMEOUT_US) == 1 &&
           i2c_read_timeout_us(i2c, address, data, Size, false, I2C_TIMEOUT_US) == static_cast<int>(Size);
}

bool Bmp580::writeRegister(uint8_t reg, uint8_t value) const
{
    const uint8_t data[] = {reg, value};
    return i2c_write_timeout_us(i2c, address, data, sizeof(data), false, I2C_TIMEOUT_US) == sizeof(data);
}

GATAS::PostConstruct Bmp580::postConstruct()
{
    // On RP2040 and RP2350 the low two GPIO bits select I2C0 SDA/SCL or I2C1 SDA/SCL.
    if (sda >= NUM_BANK0_GPIOS || scl >= NUM_BANK0_GPIOS ||
        (sda & 1) != 0 || (scl & 3) != ((sda & 3) + 1))
    {
        return GATAS::PostConstruct::CONFIG_ERROR;
    }

    i2c = (sda & 2) == 0 ? i2c0 : i2c1;
    i2c_init(i2c, 100000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    sleep_ms(2);

    for (const auto candidate : ADDRESSES)
    {
        address = candidate;
        uint8_t id[1];
        if (!readRegisters(REG_CHIP_ID, id) || id[0] != CHIP_ID)
        {
            continue;
        }
        if (!writeRegister(REG_CMD, SOFT_RESET))
        {
            return GATAS::PostConstruct::HARDWARE_ERROR;
        }
        sleep_ms(2);

        // Reset restores the factory-compensated DSP configuration.
        uint8_t status[1];
        if (!readRegisters(REG_INT_STATUS, status) || !(status[0] & RESET_COMPLETE) ||
            !readRegisters(REG_STATUS, status) || (status[0] & (NVM_READY | NVM_ERROR)) != NVM_READY ||
            !writeRegister(REG_ODR_CONFIG, STANDBY_5_HZ))
        {
            printf("Bmp580 reset or standby failed at I2C address 0x%x\n", address);
            return GATAS::PostConstruct::HARDWARE_ERROR;
        }
        sleep_ms(3);

        if (!writeRegister(REG_INT_SOURCE, DATA_READY) ||
            !writeRegister(REG_OSR_CONFIG, OVERSAMPLING) ||
            !writeRegister(REG_ODR_CONFIG, NORMAL_5_HZ))
        {
            printf("Bmp580 initialization failed at I2C address 0x%x\n", address);
            return GATAS::PostConstruct::HARDWARE_ERROR;
        }
        sleep_ms(4);
        if (!readRegisters(REG_OSR_EFF, status) || !(status[0] & ODR_VALID))
        {
            return GATAS::PostConstruct::HARDWARE_ERROR;
        }
        return GATAS::PostConstruct::OK;
    }

    printf("Bmp580 chip ID 0x50 not found at I2C addresses 0x46 or 0x47\n");
    return GATAS::PostConstruct::HARDWARE_ERROR;
}

void Bmp580::start()
{
    getBus().subscribe(*this);
}

void Bmp580::getData(etl::string_stream &stream, const etl::string_view) const
{
    stream << "{\"lastPressurehPa:hpa\":" << etl::format_spec{}.precision(1) << lastPressurehPa
           << GATAS::RESET_FORMAT << "}";
}

void Bmp580::on_receive(const GATAS::Every1SecMsg &)
{
    uint8_t status[1];
    uint8_t data[3];
    if (!readRegisters(REG_INT_STATUS, status) || !(status[0] & DATA_READY) ||
        !readRegisters(REG_PRESS_DATA, data))
    {
        return;
    }

    // The BMP580 applies factory temperature compensation internally. PRESS_DATA is unsigned
    // little-endian Q18.6 in Pa, so divide by 64 to get Pa and by 100 to get hPa.
    const uint32_t pressure = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16);
    if (pressure == 0 || pressure == 0x7f7f7f)
    {
        return;
    }

    lastPressurehPa = pressure / 6400.0f;
    const GATAS::BarometricPressure sample{lastPressurehPa, GATAS::PressureSource::PressureSensor};
    ownshipState.updateBarometricPressure(GATAS::BarometricSource::Internal, sample);
    getBus().receive(GATAS::BarometricPressureMsg{});
}
