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
    uint8_t ookGain;
    uint8_t appliedOokGain = 0;
    static uint8_t readOokGain(const Configuration &config, etl::string_view name);
    bool discardPacket = false;
    uint16_t consumedRxPackets = 0;
    uint32_t calibratedFrequency = 0;
    etl::optional<uint8_t> previousOokTiming;

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

    void receiveOOKPacket() override;

    etl::optional<uint16_t> receivedPacketCount();

    void receivePacket(uint16_t length, int16_t rssi, etl::optional<uint16_t> packetCount = etl::nullopt);

    bool discardRxBytes(uint16_t length);

    void sendPacket(const GATAS::RadioParameters &, const uint8_t *, uint8_t, uint32_t) override;

    void configureIrq(uint32_t mask);

public:
    using AbstractTransceiver::on_receive;
    void on_receive(const GATAS::ConfigUpdatedMsg &msg) override;

    // Register and schedule the LR2021 without running its hardware driver.
    static constexpr bool SCHEDULER_ONLY = false;

    bool canDelete() const override
    {
        return true;
    }

    static constexpr etl::array<etl::string_view, 4>
        NAMES{
            "Lr2021_0", "Lr2021_1", "Lr2021_2", "Lr2021_3"};

    Lr2021(etl::imessage_bus &bus, const Configuration &config, uint8_t number, uint8_t runtimeRadio)
        : AbstractTransceiver(bus, config.pinMap(NAMES[number]), runtimeRadio,
                              config.valueByPath(false, NAMES[number], "txEnabled"),
                              config.gaTasConfig().conspicuity.groundStation,
                              config.valueByPath(0, NAMES[number], "offset"), NAMES[number],
                              readCapabilities(config, NAMES[number],
                                               GATAS::LEGACY_RADIO_CAPABILITIES | static_cast<uint8_t>(GATAS::RadioCapability::Ook1090)),
                              SCHEDULER_ONLY),
          irqDio(readHardwareValue(config, NAMES[number], "irqDio", 7)),
          tcxoVoltage(readHardwareValue(config, NAMES[number], "tcxoVoltage", -1)),
          tcxoStartupUs(readHardwareValue(config, NAMES[number], "tcxoStartupUs", 5000)),
          rxSwitchDio(readHardwareValue(config, NAMES[number], "rxSwitchDio", -1)),
          txSwitchDio(readHardwareValue(config, NAMES[number], "txSwitchDio", -1)),
          ookGain(readOokGain(config, NAMES[number]))
    {
    }

    // index is the configuration slot, not the runtime radio number.
    static void enterDisabledState(uint8_t index, const Configuration &config);
};
