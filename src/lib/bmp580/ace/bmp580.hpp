#pragma once

#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/ownshipstate.hpp"

struct i2c_inst;

class Bmp580 : public BaseModule, public etl::message_router<Bmp580, GATAS::Every1SecMsg>
{
    friend class message_router;

    const uint8_t sda;
    const uint8_t scl;
    GATAS::OwnshipState &ownshipState;
    i2c_inst *i2c = nullptr;
    uint8_t address = 0;
    float lastPressurehPa = GATAS::INVALID_PRESSSURE_HPA;

    template <size_t Size>
    bool readRegisters(uint8_t reg, uint8_t (&data)[Size]) const;
    bool writeRegister(uint8_t reg, uint8_t value) const;
    void on_receive(const GATAS::Every1SecMsg &);
    void on_receive_unknown(const etl::imessage &) {}

public:
    static constexpr etl::string_view NAME = "Bmp580";

    Bmp580(etl::imessage_bus &bus, const Configuration &config, GATAS::OwnshipState &ownshipState);
    bool canDelete() const override { return true; }
    GATAS::PostConstruct postConstruct() override;
    void start() override;
    void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
