#pragma once

#include "../driver/src/sx126x.h"
#include "../driver/src/sx126x_hal.h"

/* System. */
#include <stdint.h>

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* PICO. */
#include "pico/binary_info.h"
// #include "hardware/spi.h"
#include "pico/stdlib.h"

/* Vendor. */
#include "etl/queue_mpmc_mutex.h"

/* GaTas Libraries */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"
#include "ace/spinlockguard.hpp"
#include "ace/messages.hpp"
#include "rxdataframequeue.hpp"

// https://www.waveshare.com/wiki/SX1262_XXXM_LoRaWAN/GNSS_HAT

/**
 * Client that can connect to a host and a port and expect to receive line terminated NMEA Messages
 * Part of this code taken from the example from Raspbery
 */
class Sx1262 : public Radio, public etl::message_router<Sx1262, GATAS::RadioTxFrameMsg, GATAS::ConfigUpdatedMsg, GATAS::GpsStatsMsg, GATAS::RadioControlMsg>
{
    static constexpr uint8_t MANCHESTER = 2;               // Used to just clarify why we sometime multiply by 2
#if GATAS_DEBUG == 1
    static constexpr bool LOW_POWER_MODE = true;
#else
    static constexpr bool LOW_POWER_MODE = false;
#endif
    static constexpr uint8_t LOW_POWER_DBM = 11; // - 17 (0xEF) to +14 (0x0E) dBm by step of 1 dB if low power PA is selected

    // SInce the SX1262 only has a buffer of 256 bytes, we offset the RX buffer such that we can receive large frames
    // Maximum size of ADSL TrafficUplink is 200bytes whuch would be the maximum we could receive or transmit
    // This means using this configurationw e can never have one tranceiver send and receive ADSL large frames.
    static constexpr uint8_t GROUNDSTATION_RX_BASE = 202; // Largest frame is 25 byte -> 50 in manchester But ADS-L doc seems to speak of 217 bytes
    static constexpr uint8_t DEFAULT_RX_BASE = 54;

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
        uint32_t halBusyTimeout = 0;
        uint32_t halSpiErrors = 0;
    } statistics;

    bool halError = false;

    // ************************************************************************************
    // 13.1.14.1 SetPaConfig
    // Table 13-21: PA Operating Modes with Optimal Settings
    static constexpr sx126x_pa_cfg_params_s DEFAULT_HIGH_POWER_PA_CFG =
        {
            .pa_duty_cycle = 0x04, // Never increase above 0x04, device might damage
            .hp_max = 0x07,        // never ever increase above 0x07, device might damage
            .device_sel = 0x00,    // sx1262
            .pa_lut = 0x01,        // Always 1
        };

    static constexpr sx126x_pa_cfg_params_s DEFAULT_LOW_POWER_PA_CFG =
        {
            .pa_duty_cycle = 0x02, // Never increase above 0x04, device might damage
            .hp_max = 0x02,        // never ever increase above 0x07, device might damage
            .device_sel = 0x00,    // sx1262
            .pa_lut = 0x01,        // Always 1
        };

    // ************************************************************************************
    // GFSK

    // GFSK packet is setup such that we get the last byte from teh syncword as the first byte of the packet
    // 13.4.6 SetPacketParams
    static constexpr sx126x_pkt_params_gfsk_t DEFAULT_PKG_PARAMS_GFSK =
        {
            .preamble_len_in_bits = 0,                                    // SET per protocol
            .preamble_detector = SX126X_GFSK_PREAMBLE_DETECTOR_MIN_8BITS, // SET per protocol
            .sync_word_len_in_bits = 0,                                   // SET per protocol
            .address_filtering = SX126X_GFSK_ADDRESS_FILTERING_DISABLE,
            .header_type = SX126X_GFSK_PKT_FIX_LEN,
            .pld_len_in_bytes = 0,           // SET per protocol
            .crc_type = SX126X_GFSK_CRC_OFF, // Manchester decoding used. so no CRC possible
            .dc_free = SX126X_GFSK_DC_FREE_OFF};

    // Verified, looks ok
    // 13.4.5 SetModulationParams
    static constexpr sx126x_mod_params_gfsk_t DEFAULT_MOD_PARAMS_GFSK =
        {
            .br_in_bps = 100'000,                         // 50kbps*2 (Manchester) = 100000
            .fdev_in_hz = 50'000,                         //
            .pulse_shape = SX126X_GFSK_PULSE_SHAPE_BT_05, // Gaussian BT 0.5
            .bw_dsb_param = SX126X_GFSK_BW_234300         //
        };

    // ************************************************************************************
    // LORA

    // 13.1.8 SetCAD
    // CAD is only used by LORA
    static constexpr sx126x_cad_params_t DEFAULT_CAD_PARAMS =
        {
            .cad_symb_nb = SX126X_CAD_16_SYMB,
            .cad_detect_peak = 0x14,
            .cad_detect_min = 0X0A,
            .cad_exit_mode = SX126X_CAD_ONLY,
            .cad_timeout = 0,
        };

    // 13.67
    static constexpr sx126x_pkt_params_lora_t DEFAULT_PKG_PARAMS_LORA =
        {
            .preamble_len_in_symb = 12,
            .header_type = SX126X_LORA_PKT_EXPLICIT,
            .pld_len_in_bytes = GROUNDSTATION_RX_BASE, // Based on Uplink ADSL Size so they can be teh same size, that's all
            .crc_is_on = true,
            .invert_iq_is_on = false,
        };

    static constexpr sx126x_mod_params_lora_t DEFAULT_MOD_PARAMS_LORA =
        {
            .sf = SX126X_LORA_SF7,
            .bw = SX126X_LORA_BW_250,
            .cr = SX126X_LORA_CR_4_5,
            .ldro = 0,
        };

    static constexpr GATAS::LinkLayerConfig PROTOCOL_NONE{0, GATAS::DataSource::NONE, false, 0, 16, 64, 0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}}; // NONE

    const uint8_t csPin;
    const uint8_t busyPin;
    const uint8_t dio1Pin;
    const uint8_t device;
    const uint8_t radioNo;
    bool txEnabled;
    // We need to know if the current mode is ground station so the correct package length get's set
    bool groundStation;
    uint32_t offsetHz;
    bool hasGpsFix = false;
    SpiModule *spiHall = nullptr;
    TaskHandle_t taskHandle = nullptr;
    RxDataFrameQueue *rxDataFrameQueue = nullptr;
    // Keep track of the last error when there was an error, for monitoring only
    sx126x_errors_mask_t lastDeviceError=0;
    sx126x_errors_mask_t currentDeviceError=0;
    uint8_t currentConfiguredPcId = PROTOCOL_NONE.pcId;
    GATAS::Modulation currentConfiguredModulation = GATAS::Modulation::NONE;
    etl::queue_mpmc_mutex<TxPacket, 3, etl::memory_model::MEMORY_MODEL_SMALL> txQueue;
    GATAS::RadioParameters rxRadioParameters{&PROTOCOL_NONE, &GATAS::RadioParameters::DEFAULT, 868'000'000, 0};
    // Used when new radioPatemers have arrived during RX requests
    SynchronizedValue<GATAS::RadioParameters> newRxRadioParameters{CoreUtils::sharedSpinLock(), {&PROTOCOL_NONE, &GATAS::RadioParameters::DEFAULT, 868'000'000, 0}};
    GATAS::RadioParameters lastRadioParameters{&PROTOCOL_NONE, &GATAS::RadioParameters::DEFAULT, 868'000'000, 0};

    enum class RadioDirection : uint8_t { RX, TX };

    void configureSx1262(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t payloadLength);
    void configureProtocol(const GATAS::RadioParameters &parameters);
    void configureGfskModulation(const GATAS::RadioParameters &parameters);
    void configureGfskPacket(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t payloadLength);
    void configureLoraModulation(const GATAS::RadioParameters &parameters, RadioDirection direction);
    void configureLoraPacket(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t payloadLength);

public:
    static constexpr etl::array<etl::string_view, 4> NAMES{"Sx1262_0", "Sx1262_1", "Sx1262_2", "Sx1262_3"};

    Sx1262(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t radioNo_, bool txEnabled_, bool groundStation, uint32_t offsetHz_) : Radio(bus, Radio::NAMES[radioNo_]),
                                                                                                                                                csPin(pins.at(GATAS::PinType::CS)),
                                                                                                                                                busyPin(pins.at(GATAS::PinType::BUSY)),
                                                                                                                                                dio1Pin(pins.at(GATAS::PinType::DIO1)),
                                                                                                                                                device(CoreUtils::getPin(pins, GATAS::PinType::DEV, 0)),
                                                                                                                                                radioNo(radioNo_),
                                                                                                                                                txEnabled(txEnabled_),
                                                                                                                                                groundStation(groundStation),
                                                                                                                                                offsetHz(offsetHz_)
    {
    }
    Sx1262(etl::imessage_bus &bus, const Configuration &config, uint8_t radioNo_) : Sx1262(bus,
                                                                                           config.pinMap(NAMES[radioNo_]),
                                                                                           radioNo_,
                                                                                           config.valueByPath(true, NAMES[radioNo_], "txEnabled"),
                                                                                           config.gaTasConfig().conspicuity.groundStation,
                                                                                           config.valueByPath(true, NAMES[radioNo_], "offset"))
    {

    }

    virtual ~Sx1262() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    /**
     * SPI access for SX1262 driver, don't use for anything else
     */
    inline uint8_t cs() const
    {
        return csPin;
    }
    inline uint8_t busy() const
    {
        return busyPin;
    }
    inline SpiModule *spi()
    {
        return spiHall;
    }
    virtual uint8_t radio() const
    {
        return radioNo;
    }

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    inline void sendToBus(const etl::imessage &message)
    {
        getBus().receive(message);
    };

    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::RadioTxFrameMsg &msg);
    void on_receive(const GATAS::RadioControlMsg &msg);
    void on_receive(const GATAS::GpsStatsMsg &msg);

    void radioInit();
    void checkAndClearDeviceErrors();
    void receiveGFSKPacket();
    void receiveLORAPacket();
    void sendPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length, uint32_t timeoutMs);
    sx126x_irq_mask_t getIrqStatus();
    bool isTxDone();

    void listen();
    sx126x_status_t standBy(sx126x_standby_cfg_t mode);

    static void sx1262Trampoline(void *arg);
    void sx1262Task(void *arg);
    void recordHalBusyTimeout();
    void recordHalSpiError();

    // Clear only at the start of an operation, never inside its nested helpers.
    void clearHalError()
    {
        halError = false;
    }

    bool hasHalError() const
    {
        return halError;
    }

    uint8_t receivedPacketLength() const;

    sx126x_status_t sendPacket(const TxPacket &txpacket);

    bool waitBusy(uint16_t minimumDelay = 0) const;

    /**
     * When module is disabled, this static function can be called to ensure the CS pin is not floating and the device is in sleep mode.
     */
    static void enterDisabledState(uint8_t radioNo, const Configuration &config);
};
