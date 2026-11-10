#include "../lr2021.hpp"
#include "../../lr2021driver/inc/lr20xx_radio_common.h"
#include "../../lr2021driver/inc/lr20xx_radio_fifo.h"
#include "../../lr2021driver/inc/lr20xx_radio_fsk.h"
#include "../../lr2021driver/inc/lr20xx_radio_lora.h"
#include "../../lr2021driver/inc/lr20xx_workarounds.h"
#include "../../lr2021driver/inc/lr20xx_pram.h"

bool Lr2021::check(lr20xx_status_t status)
{
    if (status != LR20XX_STATUS_OK) { halError = true; return false; }
    return true;
}

bool Lr2021::initializeRadio()
{
    const auto validDio = [](int dio) { return dio >= 5 && dio <= 11; };
    if (!validDio(irqDio) || tcxoVoltage < -1 || tcxoVoltage > 7 || tcxoStartupUs < 0 || tcxoStartupUs > 100000 ||
        (rxSwitchDio != -1 && (!validDio(rxSwitchDio) || rxSwitchDio == irqDio)) ||
        (txSwitchDio != -1 && (!validDio(txSwitchDio) || txSwitchDio == irqDio)))
    {
        GATAS_WARN("Invalid LR2021 board configuration");
        return false;
    }
    // Shared NRESET was pulsed by AceSpi. Only reset a dedicated radio pin here.
    if (resetPin != UINT8_MAX && !check(lr20xx_system_reset(this))) { return false; }
    if (!waitReady()) { return false; }
    lr20xx_system_version_t version{};
    if (!check(lr20xx_system_get_version(this, &version)) || version.major == 0 || version.major == 0xff)
    {
        return false;
    }
    GATAS_INFO("LR2021 silicon version %u.%u", version.major, version.minor);
    if (standBy(Standby::RC) != RadioStatus::OK) { return false; }
    if (!check(lr20xx_pram_load_pram_lr2021(this))) { return false; }
    if (tcxoVoltage >= 0)
    {
        if (!check(lr20xx_system_set_tcxo_mode(this, static_cast<lr20xx_system_tcxo_supply_voltage_t>(tcxoVoltage), tcxoStartupUs * 32))) { return false; }
    }
    if (!check(lr20xx_system_set_reg_mode(this, LR20XX_SYSTEM_REG_MODE_DCDC)) ||
        !check(lr20xx_system_calibrate(this, LR20XX_SYSTEM_CALIB_LF_RC_MASK | LR20XX_SYSTEM_CALIB_HF_RC_MASK |
            LR20XX_SYSTEM_CALIB_PLL_MASK | LR20XX_SYSTEM_CALIB_AAF_MASK | LR20XX_SYSTEM_CALIB_MU_MASK | LR20XX_SYSTEM_CALIB_PA_OFF_MASK)))
    {
        return false;
    }
    // Semtech LF full single-ended PA defaults. Power is limited separately.
    const lr20xx_radio_common_pa_cfg_t pa{LR20XX_RADIO_COMMON_PA_SEL_LF, LR20XX_RADIO_COMMON_PA_LF_MODE_FSM, 6, 7, 16};
    if (!check(lr20xx_radio_common_set_pa_cfg(this, &pa)) ||
        !check(lr20xx_radio_common_set_rx_path(this, LR20XX_RADIO_COMMON_RX_PATH_LF, LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_NONE)) ||
        !check(lr20xx_radio_common_set_rx_tx_fallback_mode(this, LR20XX_RADIO_FALLBACK_STDBY_XOSC))) { return false; }
    for (int dio = 5; dio <= 11; ++dio)
    {
        uint8_t mask = 0;
        if (dio == rxSwitchDio) { mask |= LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_RX_LF; }
        if (dio == txSwitchDio) { mask |= LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_LF; }
        if (mask != 0)
        {
            if (!check(lr20xx_system_set_dio_function(this, static_cast<lr20xx_system_dio_t>(dio), LR20XX_SYSTEM_DIO_FUNC_RF_SWITCH, LR20XX_SYSTEM_DIO_DRIVE_NONE)) ||
                !check(lr20xx_system_set_dio_rf_switch_cfg(this, static_cast<lr20xx_system_dio_t>(dio), mask))) { return false; }
        }
    }
    if (!check(lr20xx_system_set_dio_function(this, static_cast<lr20xx_system_dio_t>(irqDio), LR20XX_SYSTEM_DIO_FUNC_IRQ, LR20XX_SYSTEM_DIO_DRIVE_NONE)) ||
        !check(lr20xx_system_set_dio_irq_cfg(this, static_cast<lr20xx_system_dio_t>(irqDio), 0)) ||
        !check(lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK))) { return false; }
    lr20xx_system_errors_t errors = 0;
    if (!check(lr20xx_system_get_errors(this, &errors))) { return false; }
    currentDeviceError = errors;
    if (errors != 0) { lastDeviceError = errors; ++statistics.deviceErrors; return false; }
    return !hasHalError();
}

void Lr2021::configureRadio(const GATAS::RadioParameters &parameters, RadioDirection direction, uint8_t length)
{
    if (standBy(Standby::RC) != RadioStatus::OK) { return; }
    const uint32_t frequency = parameters.hopFrequency + offsetHz;
    // OpenAce currently uses the LF path. Reject unsupported frequencies explicitly.
    if (frequency < 150000000 || frequency > 960000000) { halError = true; return; }
    if (calibratedFrequency == 0 || frequency / 10000000 != calibratedFrequency / 10000000)
    {
        const lr20xx_radio_common_front_end_calibration_value_t calibration{LR20XX_RADIO_COMMON_RX_PATH_LF, frequency};
        if (!check(lr20xx_radio_common_calibrate_front_end_helper(this, &calibration, 1))) { return; }
        calibratedFrequency = frequency;
    }
    if (!check(lr20xx_radio_common_set_rf_freq(this, frequency))) { return; }
    if (parameters.frequency->mode == GATAS::Modulation::GFSK)
    {
        if (!check(lr20xx_radio_common_set_pkt_type(this, LR20XX_RADIO_COMMON_PKT_TYPE_FSK))) { return; }
        lr20xx_radio_fsk_mod_params_t modulation{};
        modulation.bitrate_unit = LR20XX_RADIO_FSK_MOD_PARAMS_BR_IN_BPS;
        modulation.bitrate = parameters.frequency->chipRate;
        modulation.fdev_in_hz = parameters.frequency->freqDiv;
        if (parameters.frequency->gaussBt != 5 && parameters.frequency->gaussBt != 10) { halError = true; return; }
        modulation.pulse_shape = parameters.frequency->gaussBt == 5 ? LR20XX_RADIO_FSK_PULSE_SHAPE_GAUSSIAN_BT_0_5 : LR20XX_RADIO_FSK_PULSE_SHAPE_GAUSSIAN_BT_1_0;
        if (!check(lr20xx_radio_fsk_get_rx_bandwidth(parameters.frequency->channelBandwidth, &modulation.bw)) ||
            !check(lr20xx_radio_fsk_set_modulation_params(this, &modulation))) { return; }
        const auto &config = *parameters.config;
        const bool transmit = direction == RadioDirection::TX;
        const auto skip = transmit ? 0 : config.syncSkipInRxLength;
        if (config.syncLength > 64 || skip >= config.syncLength) { halError = true; return; }
        const uint8_t bits = config.syncLength - skip;
        uint8_t sync[8]{};
        // Match SX1262's byte-aligned RX sync-word offset, including non-byte bit lengths.
        const size_t start = (skip + 7) / 8;
        const size_t count = (bits + 7) / 8;
        if (start + count > config.syncWord.size()) { halError = true; return; }
        for (size_t i = 0; i < count; ++i) { sync[i] = config.syncWord[start + i]; }
        const uint16_t packetLength = transmit ? length * (config.manchester ? 2 : 1) :
            (config.packetLength == 0 ? 202 : config.packetLength * (config.manchester ? 2 : 1));
        if (packetLength > 255) { halError = true; return; }
        const lr20xx_radio_fsk_pkt_params_t packet{
            config.txPreambleLength, LR20XX_RADIO_FSK_PREAMBLE_DETECTOR_8_BITS, false,
            LR20XX_RADIO_FSK_ADDRESS_FILTERING_DISABLED,
            config.packetLength == 0 ? LR20XX_RADIO_FSK_HEADER_8BITS : LR20XX_RADIO_FSK_HEADER_IMPLICIT,
            LR20XX_RADIO_FSK_PAYLOAD_LENGTH_IN_BYTE, packetLength, LR20XX_RADIO_FSK_CRC_OFF, LR20XX_RADIO_FSK_WHITENING_OFF};
        if (!check(lr20xx_radio_fsk_set_packet_params(this, &packet)) ||
            !check(lr20xx_radio_fsk_set_syncword(this, sync, bits, LR20XX_RADIO_FSK_SYNCWORD_MSBF))) { return; }
    }
    else if (parameters.frequency->mode == GATAS::Modulation::LORA)
    {
        if (!check(lr20xx_radio_common_set_pkt_type(this, LR20XX_RADIO_COMMON_PKT_TYPE_LORA))) { return; }
        lr20xx_radio_lora_mod_params_t modulation{LR20XX_RADIO_LORA_SF7, LR20XX_RADIO_LORA_BW_250, LR20XX_RADIO_LORA_CR_4_5, LR20XX_RADIO_LORA_NO_PPM};
        switch (parameters.frequency->channelBandwidth)
        {
        case 125000: modulation.bw = LR20XX_RADIO_LORA_BW_125; break;
        case 250000: modulation.bw = LR20XX_RADIO_LORA_BW_250; break;
        case 500000: modulation.bw = LR20XX_RADIO_LORA_BW_500; break;
        default: halError = true; return;
        }
        if (direction == RadioDirection::TX)
        {
            if (parameters.codingRate == 6) { modulation.cr = LR20XX_RADIO_LORA_CR_4_6; }
            if (parameters.codingRate == 7) { modulation.cr = LR20XX_RADIO_LORA_CR_4_7; }
        }
        const lr20xx_radio_lora_pkt_params_t packet{parameters.config->txPreambleLength, LR20XX_RADIO_LORA_PKT_EXPLICIT,
            static_cast<uint8_t>(direction == RadioDirection::TX ? length : (parameters.config->packetLength == 0 ? 255 : parameters.config->packetLength)),
            LR20XX_RADIO_LORA_CRC_ENABLED, LR20XX_RADIO_LORA_IQ_STANDARD};
        if (!check(lr20xx_radio_lora_set_modulation_params(this, &modulation)) ||
            !check(lr20xx_radio_lora_set_packet_params(this, &packet)) ||
            !check(lr20xx_radio_lora_set_syncword(this, parameters.config->syncWord[0])) ||
            !check(lr20xx_workarounds_lora_enable_sx1276_compatibility_mode(this))) { return; }
    }
    else { halError = true; return; }
    currentConfiguredPcId = parameters.config->pcId;
    currentConfiguredModulation = parameters.frequency->mode;
    lastRadioParameters = parameters;
}

void Lr2021::configureIrq(uint32_t mask)
{
    disablePinInterrupt(dio1Pin);
    check(lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK));
    check(lr20xx_system_set_dio_irq_cfg(this, static_cast<lr20xx_system_dio_t>(irqDio), mask |
        LR20XX_SYSTEM_IRQ_ERROR | LR20XX_SYSTEM_IRQ_CMD_ERROR));
    enablePinInterrupt(dio1Pin, DIO1_EVENT);
}

void Lr2021::listen()
{
    configureIrq(LR20XX_SYSTEM_IRQ_RX_DONE | LR20XX_SYSTEM_IRQ_TIMEOUT | LR20XX_SYSTEM_IRQ_CRC_ERROR | LR20XX_SYSTEM_IRQ_LEN_ERROR);
    discardPacket = false;
    if (hasHalError() || !check(lr20xx_radio_fifo_clear_rx(this))) { return; }
    check(lr20xx_radio_common_set_rx_with_timeout_in_rtc_step(this, 0));
}

void Lr2021::sendPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length, uint32_t timeout)
{
    configureIrq(LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_TIMEOUT);
    const int8_t power = LOW_POWER_MODE ? LOW_POWER_DBM : etl::max<int8_t>(-10, etl::min<int8_t>(22, parameters.frequency->powerdBm));
    if (hasHalError() || !check(lr20xx_radio_common_set_tx_params(this, power * 2, LR20XX_RADIO_COMMON_RAMP_208_US)) ||
        !check(lr20xx_radio_fifo_clear_tx(this)) || !check(lr20xx_radio_fifo_write_tx(this, data, length))) { return; }
    check(lr20xx_radio_common_set_tx(this, timeout));
}

AbstractTransceiver::RadioStatus Lr2021::standBy(Standby mode)
{
    return check(lr20xx_system_set_standby_mode(this, mode == Standby::RC ? LR20XX_SYSTEM_STANDBY_MODE_RC : LR20XX_SYSTEM_STANDBY_MODE_XOSC)) ? RadioStatus::OK : RadioStatus::ERROR;
}

etl::optional<AbstractTransceiver::Irq> Lr2021::readIrq()
{
    lr20xx_system_stat1_t first{};
    lr20xx_system_stat2_t second{};
    uint32_t raw = 0;
    if (!check(lr20xx_system_get_status(this, &first, &second, &raw))) { return etl::nullopt; }
    uint8_t events = 0;
    discardPacket = (raw & (LR20XX_SYSTEM_IRQ_CRC_ERROR | LR20XX_SYSTEM_IRQ_LEN_ERROR | LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR)) != 0;
    if (raw & LR20XX_SYSTEM_IRQ_RX_DONE) { events |= IRQ_RX_DONE; }
    if (raw & LR20XX_SYSTEM_IRQ_TX_DONE) { events |= IRQ_TX_DONE; }
    if (raw & LR20XX_SYSTEM_IRQ_TIMEOUT) { events |= IRQ_TIMEOUT; }
    if (discardPacket && !(events & IRQ_RX_DONE)) { events |= IRQ_TIMEOUT; }
    if (raw & (LR20XX_SYSTEM_IRQ_ERROR | LR20XX_SYSTEM_IRQ_CMD_ERROR))
    {
        ++statistics.deviceErrors;
        check(lr20xx_system_get_errors(this, &currentDeviceError));
        lastDeviceError = currentDeviceError;
        check(lr20xx_system_clear_errors(this));
        // Acknowledge once and let the shared recovery path restore RX.
        check(lr20xx_system_clear_irq_status(this, raw));
        halError = true;
        return etl::nullopt;
    }
    return Irq{raw, events};
}

AbstractTransceiver::RadioStatus Lr2021::acknowledgeIrq(uint32_t raw)
{
    return check(lr20xx_system_clear_irq_status(this, raw)) ? RadioStatus::OK : RadioStatus::ERROR;
}

void Lr2021::receivePacket(uint16_t length, int16_t rssi)
{
    if (discardPacket || length < 4 || length > 255 ||
        (lastRadioParameters.config->manchester && length % 2 != 0) ||
        (lastRadioParameters.config->dataSource() == GATAS::DataSource::ADSLO_HDR && length % 4 != 0)) { return; }
    if (auto data = static_cast<uint8_t *>(getGlobalPool().alloc(length)))
    {
        GATAS::DataFrame frame{.epochSeconds = CoreUtils::secondsSinceEpoch(),
            .frequency = lastRadioParameters.hopFrequency, .config = lastRadioParameters.config,
            .frame = {getGlobalPool(), data}, .length = static_cast<uint8_t>(length),
            .rssidBm = static_cast<int8_t>(etl::max<int16_t>(-128, etl::min<int16_t>(127, rssi)))};
        if (check(lr20xx_radio_fifo_read_rx(this, data, length)))
        {
            ++statistics.receivedPackets;
            rxDataFrameQueue->push(etl::move(frame));
        }
    }
}

void Lr2021::receiveGFSKPacket()
{
    lr20xx_radio_fsk_packet_status_t packet{};
    if (check(lr20xx_radio_fsk_get_packet_status(this, &packet))) { receivePacket(packet.packet_length_bytes, packet.rssi_avg_in_dbm); }
}
void Lr2021::receiveLORAPacket()
{
    lr20xx_radio_lora_packet_status_t packet{};
    if (check(lr20xx_radio_lora_get_packet_status(this, &packet))) { receivePacket(packet.packet_length_bytes, packet.rssi_signal_pkt_in_dbm); }
}

void Lr2021::enterDisabledState(uint8_t number, const Configuration &config)
{
    const auto pins = config.pinMap(NAMES[number]);
    const auto cs = CoreUtils::getPin(pins, GATAS::PinType::CS, UINT8_MAX);
    if (cs < NUM_BANK0_GPIOS) { gpio_init(cs); gpio_put(cs, 1); gpio_set_dir(cs, GPIO_OUT); }
}
