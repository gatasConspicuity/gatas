#pragma once
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"
#include "ace/messages.hpp"
#include "etl/queue_mpmc_mutex.h"
#include "etl/optional.h"
#include "rxdataframequeue.hpp"
#include "radiohardware.hpp"

// Shared task, queue ownership and message contract for packet transceivers.
class AbstractTransceiver : public Radio, public RadioHardware,
    public etl::message_router<AbstractTransceiver, GATAS::RadioTxFrameMsg, GATAS::ConfigUpdatedMsg, GATAS::GpsStatsMsg, GATAS::RadioControlMsg>
{
protected:
    static constexpr uint8_t MANCHESTER = 2;
#if GATAS_DEBUG == 1
    static constexpr bool LOW_POWER_MODE = true;
#else
    static constexpr bool LOW_POWER_MODE = false;
#endif
    static constexpr uint8_t LOW_POWER_DBM = 11;
    enum class RadioStatus { OK, ERROR };
    enum class Standby { RC, XOSC };
    enum : uint8_t { IRQ_RX_DONE = 1, IRQ_TX_DONE = 2, IRQ_TIMEOUT = 4 };
    struct Irq { uint32_t raw; uint8_t events; };
    const etl::string_view moduleName;
    enum TaskState : uint8_t
    {
        DIO1_EVENT = 1,
        TX_PENDING = 4,
        RX_CONFIG_PENDING = 8,
    };

    struct TxPacket
    {
        GATAS::RadioParameters radioParameters;
        PoolOwnedPtr<GATAS::GlobalPoolConfiguration, const uint8_t> frame;
        size_t length = 0;
    };

    mutable struct
    {
        uint16_t deviceErrors = 0;
        uint32_t receivedPackets = 0;
        uint32_t transmittedPackets = 0;
        uint32_t buzyWaitsTimeout = 0;
        uint32_t queueFull = 0;
        uint32_t txConfigureStarted = 0;
        uint32_t txConfigureCompleted = 0;
        uint32_t txSendPacketCompleted = 0;
        uint32_t txTimeout = 0;
        uint32_t rxTimeout = 0;
        uint32_t unexpectedTimeout = 0;
        uint32_t txRecovery = 0;
    } statistics;

    static constexpr GATAS::LinkLayerConfig PROTOCOL_NONE{0, GATAS::DataSource::NONE, false, 0, 16, 64, 0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}}; // NONE

    const uint8_t device;
    const uint8_t radioNo;
    bool txEnabled;
    // We need to know if the current mode is ground station so the correct package length get's set
    bool groundStation;
    uint32_t offsetHz;
    bool hasGpsFix = false;
    TaskHandle_t taskHandle = nullptr;
    RxDataFrameQueue *rxDataFrameQueue = nullptr;
    // Keep track of the last error when there was an error, for monitoring only
    uint16_t lastDeviceError = 0;
    uint16_t currentDeviceError = 0;
    uint8_t currentConfiguredPcId = PROTOCOL_NONE.pcId;
    GATAS::Modulation currentConfiguredModulation = GATAS::Modulation::NONE;
    etl::queue_mpmc_mutex<TxPacket, 3, etl::memory_model::MEMORY_MODEL_SMALL> txQueue;
    GATAS::RadioParameters rxRadioParameters{&PROTOCOL_NONE, &GATAS::RadioParameters::DEFAULT, 868'000'000, 0};
    // Used when new radioPatemers have arrived during RX requests
    GATAS::RadioParameters newRxRadioParameters{&PROTOCOL_NONE, &GATAS::RadioParameters::DEFAULT, 868'000'000, 0};
    GATAS::RadioParameters lastRadioParameters{&PROTOCOL_NONE, &GATAS::RadioParameters::DEFAULT, 868'000'000, 0};

    enum class RadioDirection : uint8_t { RX, TX };

    virtual bool initializeRadio() = 0;
    virtual void configureRadio(const GATAS::RadioParameters &, RadioDirection, uint8_t) = 0;
    virtual void listen() = 0;
    virtual RadioStatus standBy(Standby) = 0;
    virtual etl::optional<Irq> readIrq() = 0;
    virtual RadioStatus acknowledgeIrq(uint32_t) = 0;
    virtual void receiveGFSKPacket() = 0;
    virtual void receiveLORAPacket() = 0;
    virtual void sendPacket(const GATAS::RadioParameters &, const uint8_t *, uint8_t, uint32_t) = 0;
    RadioStatus sendQueuedPacket(const TxPacket &);
    static void taskTrampoline(void *);
    void radioTask(void *);

public:
    AbstractTransceiver(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t number,
                        bool transmit, bool ground, uint32_t offset, etl::string_view name)
        : Radio(bus, Radio::NAMES[number]), RadioHardware(pins), moduleName(name),
          device(CoreUtils::getPin(pins, GATAS::PinType::DEV, 0)), radioNo(number),
          txEnabled(transmit), groundStation(ground), offsetHz(offset) {}
    GATAS::PostConstruct postConstruct() override;
    void start() override;
    uint8_t radio() const override { return radioNo; }
    void getData(etl::string_stream &, etl::string_view) const override;
    void on_receive(const GATAS::ConfigUpdatedMsg &);
    void on_receive(const GATAS::RadioTxFrameMsg &);
    void on_receive(const GATAS::RadioControlMsg &);
    void on_receive(const GATAS::GpsStatsMsg &);
    void on_receive_unknown(const etl::imessage &) {}
};
