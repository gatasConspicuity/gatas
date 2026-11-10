#pragma once
#include "abstracttransceiver.hpp"
#include "../sx1262driver/src/sx126x.h"
#include "../sx1262driver/src/sx126x_hal.h"

class Sx1262 : public AbstractTransceiver
{
    // SInce the SX1262 only has a buffer of 256 bytes, we offset the RX buffer such that we can receive large frames
    // Maximum size of ADSL TrafficUplink is 200bytes whuch would be the maximum we could receive or transmit
    // This means using this configurationw e can never have one tranceiver send and receive ADSL large frames.
    static constexpr uint8_t GROUNDSTATION_RX_BASE = 202; // Largest frame is 25 byte -> 50 in manchester But ADS-L doc seems to speak of 217 bytes
    static constexpr uint8_t DEFAULT_RX_BASE = 54;

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

protected:
    void configureRadio(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t payloadLength) override;
    void configureProtocol(const GATAS::RadioParameters &parameters);
    void configureGfskModulation(const GATAS::RadioParameters &parameters);
    void configureGfskPacket(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t payloadLength);
    void configureLoraModulation(const GATAS::RadioParameters &parameters, RadioDirection direction);
    void configureLoraPacket(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t payloadLength);

    bool initializeRadio() override;
    void radioInit();
    void checkAndClearDeviceErrors();
    void receiveGFSKPacket() override;
    void receiveLORAPacket() override;
    void sendPacket(const GATAS::RadioParameters &, const uint8_t *, uint8_t, uint32_t) override;
    void listen() override;
    RadioStatus standBy(Standby mode) override;
    etl::optional<Irq> readIrq() override;
    RadioStatus acknowledgeIrq(uint32_t raw) override;
    uint8_t receivedPacketLength() const;
    bool waitBusy(uint16_t minimumDelay) const;
public:
    bool canDelete() const override
    {
        return true;
    }

    static constexpr etl::array<etl::string_view, 4> NAMES{"Sx1262_0", "Sx1262_1", "Sx1262_2", "Sx1262_3"};

    Sx1262(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t radioNo_, bool txEnabled_, bool groundStation, uint32_t offsetHz_) : AbstractTransceiver(bus, pins, radioNo_, txEnabled_, groundStation, offsetHz_, NAMES[radioNo_]) {}
    Sx1262(etl::imessage_bus &bus, const Configuration &config, uint8_t radioNo_) : Sx1262(bus,
                                                                                           config.pinMap(NAMES[radioNo_]),
                                                                                           radioNo_,
                                                                                           config.valueByPath(true, NAMES[radioNo_], "txEnabled"),
                                                                                           config.gaTasConfig().conspicuity.groundStation,
                                                                                           config.valueByPath(0, NAMES[radioNo_], "offset"))
    {

    }

    static void enterDisabledState(uint8_t radioNo, const Configuration &config);
};
