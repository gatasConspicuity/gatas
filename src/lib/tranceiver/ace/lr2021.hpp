#pragma once
#include "abstracttransceiver.hpp"
#include "../lr2021driver/inc/lr20xx_system.h"

class Lr2021 : public AbstractTransceiver
{
    const int irqDio;
    const int tcxoVoltage; // -1 selects crystal; otherwise Semtech voltage enum 0..7
    const int tcxoStartupUs;
    const int rxSwitchDio;
    const int txSwitchDio;
    bool discardPacket = false;
    uint32_t calibratedFrequency = 0;
protected:
    bool check(lr20xx_status_t status);
    bool initializeRadio() override;
    void configureRadio(const GATAS::RadioParameters &, RadioDirection, uint8_t) override;
    void listen() override;
    RadioStatus standBy(Standby) override;
    etl::optional<Irq> readIrq() override;
    RadioStatus acknowledgeIrq(uint32_t) override;
    void receiveGFSKPacket() override;
    void receiveLORAPacket() override;
    void receivePacket(uint16_t length, int16_t rssi);
    void sendPacket(const GATAS::RadioParameters &, const uint8_t *, uint8_t, uint32_t) override;
    void configureIrq(uint32_t mask);
public:
    bool canDelete() const override
    {
        return true;
    }

    static constexpr etl::array<etl::string_view, 4> NAMES{"Lr2021_0", "Lr2021_1", "Lr2021_2", "Lr2021_3"};
    Lr2021(etl::imessage_bus &bus, const Configuration &config, uint8_t number)
        : AbstractTransceiver(bus, config.pinMap(NAMES[number]), number,
              config.valueByPath(false, NAMES[number], "txEnabled"),
              config.gaTasConfig().conspicuity.groundStation,
              config.valueByPath(0, NAMES[number], "offset"), NAMES[number]),
          irqDio(config.valueByPath(7, NAMES[number], "irqDio")),
          tcxoVoltage(config.valueByPath(-1, NAMES[number], "tcxoVoltage")),
          tcxoStartupUs(config.valueByPath(5000, NAMES[number], "tcxoStartupUs")),
          rxSwitchDio(config.valueByPath(-1, NAMES[number], "rxSwitchDio")),
          txSwitchDio(config.valueByPath(-1, NAMES[number], "txSwitchDio")) {}
    static void enterDisabledState(uint8_t number, const Configuration &config);
};
