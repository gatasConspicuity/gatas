#pragma once

#include <stdint.h>

#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"
#include "ace/messages.hpp"
#include "ace/ownshipstate.hpp"

/**
 * Client that can connect to a host and a port and expect to receive line terminated NMEA Messages
 * Part of this code taken from the example from Raspbery
 */
class Bmp280 : public BaseModule, public etl::message_router<Bmp280, GATAS::ConfigUpdatedMsg, GATAS::Every1SecMsg>
{
    friend class message_router;
    struct
    {
        float lastPressurehPa = 1013;
    } statistics;

    static constexpr uint8_t READ_BIT = 0x80;

    const uint8_t cs;
    const uint8_t device;
    int16_t compensation;
    GATAS::OwnshipState &ownshipState;

    int32_t t_fine = 0;
    uint16_t dig_T1 = 0;
    int16_t dig_T2 = 0, dig_T3 = 0;
    uint16_t dig_P1 = 0;
    int16_t dig_P2 = 0, dig_P3 = 0, dig_P4 = 0, dig_P5 = 0, dig_P6 = 0, dig_P7 = 0, dig_P8 = 0, dig_P9 = 0;
    SpiModule *aceSpi = nullptr;

private:
    /* The following compensation functions are required to convert from the raw ADC
    data from the chip to something usable. Each chip has a different set of
    compensation parameters stored on the chip at point of manufacture, which are
    read from the chip at startup and used in these routines.
    */
    int32_t compensate_temp(int32_t adc_T);
    uint32_t compensate_pressure(int32_t adc_P);
    /* This function reads the manufacturing assigned compensation parameters from the device */
    void read_compensation_parameters();

    void on_receive_unknown(const etl::imessage &msg);

    void on_receive(const GATAS::ConfigUpdatedMsg &msg);

    void on_receive(const GATAS::Every1SecMsg &msg);

public:
    static constexpr const etl::string_view NAME = "Bmp280";
    Bmp280(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, int16_t compensation_, GATAS::OwnshipState &ownshipState_) : BaseModule(bus, NAME),
                                                                                           cs(pins.at(GATAS::PinType::CS)),
                                                                                           device(CoreUtils::getPin(pins, GATAS::PinType::DEV, 0)),
                                                                                           compensation(compensation_),
                                                                                           ownshipState(ownshipState_)
    {
    }

    Bmp280(etl::imessage_bus &bus, const Configuration &config, GATAS::OwnshipState &ownshipState_) : Bmp280(bus, config.pinMap(NAME), config.valueByPath(0, NAME, "compensation"), ownshipState_)
    {
    }

    virtual ~Bmp280() = default;

    bool canDelete() const override
    {
        return true;
    }

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
