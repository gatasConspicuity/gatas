#include "../sx1262.hpp"

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ETL. */
#include "etl/map.h"

/* GATAS. */
#include "ace/manchester.hpp"
#include "ace/coreutils.hpp"
#include "ace/measure.hpp"
#include "ace/models.hpp"
#include "ace/moreutils.hpp"

;

void Sx1262::enterDisabledState(uint8_t index, const Configuration &config)
{
    auto pinMap = config.pinMap(NAMES[index]);
    auto csPin = CoreUtils::getPin(pinMap, GATAS::PinType::CS, UINT8_MAX);
    if (csPin >= NUM_BANK0_GPIOS) { return; }
    gpio_init(csPin);
    gpio_put(csPin, 1);
    gpio_set_dir(csPin, GPIO_OUT);
}

/**
 * Apply methods
 */

void Sx1262::radioInit()
{
    sx126x_reset(this);

    // .365
    waitBusy(50);

    sx126x_set_dio_irq_params(this, SX126X_IRQ_NONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    sx126x_clear_irq_status(this, SX126X_IRQ_ALL);

    // Must be called before any calibration
    auto rtcSteps = sx126x_convert_timeout_in_ms_to_rtc_step(5);
    sx126x_set_dio3_as_tcxo_ctrl(this, SX126X_TCXO_CTRL_1_6V, rtcSteps);

    sx126x_set_dio2_as_rf_sw_ctrl(this, true);

    // Start Calibration

    // 9.4 Standby (STDBY) Mode DCDC must be set in SX126X_STANDBY_CFG_RC mode
    // 13.1.12 Calibrate Function and regulator must happen in SX126X_STANDBY_CFG_RC
    sx126x_set_standby(this, SX126X_STANDBY_CFG_RC);
    sx126x_set_reg_mode(this, SX126X_REG_MODE_DCDC);
    sx126x_cal(this, SX126X_CAL_ALL);
    waitBusy(25);
    checkAndClearDeviceErrors();
    sx126x_cal_img_in_mhz(this, 868 - 2, 868 + 2); // 13.1.13 CalibrateImage
    waitBusy(100);
    // Clear out any devices errors that will appear before we did the first calibration
    statistics.deviceErrors = 0;
    lastDeviceError = 0;
    checkAndClearDeviceErrors();

    // Calibration done, use stanby
    standBy(Standby::RC);

    // 15.2.2 Better Resistance of the SX1262 Tx to Antenna Mismatch
    // uint8_t clamp;
    // sx126x_read_register(this, 0x08D8, &clamp, 1);
    // clamp |= 0x1E;
    // sx126x_write_register(this, 0x08D8, &clamp, 1);
    sx126x_cfg_tx_clamp(this);

    // TX Base at 0x00  RX Base at 0x80
    sx126x_set_buffer_base_address(this, 0x00, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE);

    sx126x_set_rx_tx_fallback_mode(this, SX126X_FALLBACK_STDBY_XOSC);
    // sx126x_set_cad_params(this, &DEFAULT_CAD_PARAMS);

    sx126x_set_pa_cfg(this, &DEFAULT_HIGH_POWER_PA_CFG);
    sx126x_set_ocp_value(this, SX126X_OCP_PARAM_VALUE_140_MA);

    // Add RX gain register to retention
    const uint16_t reg = 0x08AC;
    sx126x_add_registers_to_retention_list(this, &reg, 1);

    // Set boosted gain once
    sx126x_cfg_rx_boosted(this, true);
}

void Sx1262::configureRadio(const GATAS::RadioParameters &newParameters,
                             RadioDirection direction, uint8_t payloadLength)
{
    GATAS_ASSERT(newParameters.config != nullptr && newParameters.frequency != nullptr, "Invalid radio parameters");
    GATAS_ASSERT(!newParameters.config->isIdle(), "Cannot configure radio for NONE");
    GATAS_MEASURE("configureRadio", 900);

    // Completion/timeout leaves the radio in standby; the task stops active RX.
    // Only a modem change requires selecting RC standby here.
    if (newParameters.frequency->mode != currentConfiguredModulation)
    {
        standBy(Standby::RC);
    }

    {
        GATAS_MEASURE("checkAndClearDeviceErrors", 600 /* 200 */);
        checkAndClearDeviceErrors();
    }

    if (newParameters.config->pcId != currentConfiguredPcId)
    {
        configureProtocol(newParameters);
    }

    // SX1261/2 section 14.4: packet type, then modulation, then packet parameters.
    // GFSK modulation is fixed per protocol; LoRa TX coding rate is per packet.
    sx126x_set_buffer_base_address(this, 0x00, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE);
    if (newParameters.frequency->mode == GATAS::Modulation::GFSK)
    {
        configureGfskModulation(newParameters);
        configureGfskPacket(newParameters, direction, payloadLength);
    }
    else if (newParameters.frequency->mode == GATAS::Modulation::LORA)
    {
        configureLoraModulation(newParameters, direction);
        configureLoraPacket(newParameters, direction, payloadLength);
    }

    sx126x_set_rf_freq(this, newParameters.hopFrequency + offsetHz);
    {
        GATAS_MEASURE("checkAndClearDeviceErrors", 600 /* 200 */);
        checkAndClearDeviceErrors();
    }

    currentConfiguredPcId = newParameters.config->pcId;
    currentConfiguredModulation = newParameters.frequency->mode;
    lastRadioParameters = newParameters;
}

void Sx1262::configureProtocol(const GATAS::RadioParameters &newParameters)
{
    GATAS_MEASURE("configureProtocol", 1600 /* 500 */);
    // Recovery also invalidates this cache if the HAL records a setup error.
    currentConfiguredPcId = PROTOCOL_NONE.pcId;
    currentConfiguredModulation = GATAS::Modulation::NONE;

    sx126x_set_pa_cfg(this, LOW_POWER_MODE ? &DEFAULT_LOW_POWER_PA_CFG : &DEFAULT_HIGH_POWER_PA_CFG);
    if (newParameters.frequency->mode == GATAS::Modulation::GFSK)
    {
        sx126x_set_pkt_type(this, SX126X_PKT_TYPE_GFSK);
    }
    else if (newParameters.frequency->mode == GATAS::Modulation::LORA)
    {
        sx126x_set_pkt_type(this, SX126X_PKT_TYPE_LORA);
    }
}

void Sx1262::configureGfskModulation(const GATAS::RadioParameters &newParameters)
{
    // Unlike LoRa's per-packet coding rate, GFSK modulation is fixed by pcId.
    // The coordinator commits the new pcId only after all configuration calls.
    if (newParameters.config->pcId == currentConfiguredPcId)
    {
        return;
    }

    auto mod = DEFAULT_MOD_PARAMS_GFSK;
    if (newParameters.frequency->channelBandwidth == 234300)
    {
        mod.bw_dsb_param = SX126X_GFSK_BW_234300;
    }
    else if (newParameters.frequency->channelBandwidth == 312000)
    {
        mod.bw_dsb_param = SX126X_GFSK_BW_312000;
    }
    else if (newParameters.frequency->channelBandwidth == 187200)
    {
        mod.bw_dsb_param = SX126X_GFSK_BW_187200;
    }
    else
    {
        GATAS_WARN("bw_dsb_param Not set");
    }

    if (newParameters.frequency->gaussBt == 10)
    {
        mod.pulse_shape = SX126X_GFSK_PULSE_SHAPE_BT_1;
    }
    else if (newParameters.frequency->gaussBt == 5)
    {
        mod.pulse_shape = SX126X_GFSK_PULSE_SHAPE_BT_05;
    }
    else
    {
        GATAS_WARN("gaussBt Not set");
    }

    mod.br_in_bps = newParameters.frequency->chipRate;
    mod.fdev_in_hz = newParameters.frequency->freqDiv;

    sx126x_set_gfsk_mod_params(this, &mod);
}

void Sx1262::configureGfskPacket(const GATAS::RadioParameters &newParameters,
                                 RadioDirection direction, uint8_t payloadLength)
{
    // preamble_len_in_bits -> transmitted preamble length: number of bits sent as preamble coded as 0x55.
    // preamble_detector -> the packet controller will only become active if a certain number of preamble bits have been successfully received by the rad
    // The user can select a value ranging from “Preamble detector length off” - where the radio will not perform any gating and will try to lock directly on the following Sync Word
    auto pkt_params_gfsk = DEFAULT_PKG_PARAMS_GFSK;
    uint8_t syncLengthBits;
    const uint8_t *syncData;

    if (direction == RadioDirection::TX)
    {
        // TX Config
        syncLengthBits = newParameters.config->syncLength;
        syncData = newParameters.config->syncWord.data();
        pkt_params_gfsk.pld_len_in_bytes = payloadLength * (newParameters.config->manchester ? 2 : 1);

        // Variable payload test (0 is true)
        if (newParameters.config->packetLength == 0) // When newParameters.config->packetLength == 0, this means variable payload
        {
            pkt_params_gfsk.header_type = SX126X_GFSK_PKT_VAR_LEN;
        }
    }
    else
    {
        // RX Config
        syncLengthBits = newParameters.config->syncLength - newParameters.config->syncSkipInRxLength;
        syncData = newParameters.config->syncWord.data() + (newParameters.config->syncSkipInRxLength + 7) / 8;

        // Variable payload test (0 is true)
        if (newParameters.config->packetLength == 0)
        {
            pkt_params_gfsk.header_type = SX126X_GFSK_PKT_VAR_LEN;
            // Maximum payload size in variable mode
            pkt_params_gfsk.pld_len_in_bytes = GROUNDSTATION_RX_BASE;
        }
        else
        {
            pkt_params_gfsk.pld_len_in_bytes = newParameters.config->packetLength * (newParameters.config->manchester ? 2 : 1);
        }
    }

    pkt_params_gfsk.preamble_detector = SX126X_GFSK_PREAMBLE_DETECTOR_MIN_8BITS;   // Reception (bit set anyways) Must be smaller than sync 6.2.2.1
    pkt_params_gfsk.preamble_len_in_bits = newParameters.config->txPreambleLength; // In addition to this length, there is also 16 bit preamble specific for nRF905 added to the syncword. This will works fine for an SX1262
    pkt_params_gfsk.sync_word_len_in_bits = syncLengthBits;
    sx126x_set_gfsk_pkt_params(this, &pkt_params_gfsk);
    sx126x_set_gfsk_sync_word(this, syncData, (syncLengthBits + 7) / 8);
}

void Sx1262::configureLoraModulation(const GATAS::RadioParameters &newParameters, RadioDirection direction)
{
    auto mod = DEFAULT_MOD_PARAMS_LORA;
    if (newParameters.frequency->channelBandwidth == 500000)
    {
        mod.bw = SX126X_LORA_BW_500;
    }
    else if (newParameters.frequency->channelBandwidth == 250000)
    {
        mod.bw = SX126X_LORA_BW_250;
    }
    else if (newParameters.frequency->channelBandwidth == 125000)
    {
        mod.bw = SX126X_LORA_BW_125;
    }

    // TX coding rate is selected per packet; RX restores the default.
    if (direction == RadioDirection::TX)
    {
        switch (newParameters.codingRate)
        {
        case 5:
            mod.cr = SX126X_LORA_CR_4_5;
            break;
        case 6:
            mod.cr = SX126X_LORA_CR_4_6;
            break;
        case 7:
            mod.cr = SX126X_LORA_CR_4_7;
            break;
        }
    }

    // SX1261/2 section 14.4: packet type, then modulation, then packet parameters.
    sx126x_set_lora_mod_params(this, &mod);
}

void Sx1262::configureLoraPacket(const GATAS::RadioParameters &newParameters,
                                 RadioDirection direction, uint8_t payloadLength)
{
    auto pkg = DEFAULT_PKG_PARAMS_LORA;
    pkg.pld_len_in_bytes = direction == RadioDirection::TX ? payloadLength : newParameters.config->packetLength;
    pkg.preamble_len_in_symb = newParameters.config->txPreambleLength;
    sx126x_set_lora_pkt_params(this, &pkg);
    // Keep sync-word setup with packet settings for both modems. LoRa's sync
    // word is direction-independent, so it only needs writing on a pcId change.
    if (newParameters.config->pcId != currentConfiguredPcId)
    {
        sx126x_set_lora_sync_word(this, newParameters.config->syncWord.data()[0]);
    }
}

void Sx1262::listen()
{
    sx126x_cfg_rx_boosted(this, true);
    consumedRxCounts = {};
    pendingRxRead = false;
    pendingLoraCrcError = false;
    sx126x_reset_stats(this);

    disablePinInterrupt(dio1Pin);
    sx126x_clear_irq_status(this, SX126X_IRQ_ALL);
    sx126x_set_dio_irq_params(this, SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    enablePinInterrupt(dio1Pin, DIO1_EVENT);

    sx126x_set_rx_with_timeout_in_rtc_step(this, SX126X_RX_CONTINUOUS);
}

void Sx1262::sendPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length, uint32_t timeoutMs)
{
    disablePinInterrupt(dio1Pin);
    sx126x_clear_irq_status(this, SX126X_IRQ_ALL);
    sx126x_set_dio_irq_params(this, SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    enablePinInterrupt(dio1Pin, DIO1_EVENT);

    // 22Dbm is the max power for sx1262.
    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::min(static_cast<int8_t>(22), parameters.frequency->powerdBm);
    sx126x_set_tx_params(this, powerdBm, SX126X_RAMP_200_US);
    sx126x_write_buffer(this, 0x00, data, length);
    // 13.1.14 SetTx
    sx126x_set_tx(this, timeoutMs);
}

void Sx1262::receiveGFSKPacket()
{
    receivePacket();
}

void Sx1262::receiveLORAPacket()
{
    receivePacket();
}

etl::optional<Sx1262::RxCounts> Sx1262::receivedPacketCounts()
{
    if (lastRadioParameters.frequency->mode == GATAS::Modulation::LORA)
    {
        sx126x_stats_lora_t counts{};
        if (sx126x_get_lora_stats(this, &counts) == SX126X_STATUS_OK)
        {
            return RxCounts{counts.nb_pkt_received, counts.nb_pkt_crc_error};
        }
    }
    else
    {
        sx126x_stats_gfsk_t counts{};
        if (sx126x_get_gfsk_stats(this, &counts) == SX126X_STATUS_OK)
        {
            // Our GFSK profiles disable hardware CRC. Protocol checks run after
            // software decoding, so this hardware counter is not applicable.
            return RxCounts{counts.nb_pkt_received, 0};
        }
    }
    return etl::nullopt;
}

void Sx1262::receivePacket()
{
    pendingRxRead = false;
    // The chip retains only the latest packet's status. Check its completion
    // count around the read so a newer packet cannot mix payload and metadata.
    // Bound retries to keep the SPI bus available to other devices.
    for (uint8_t attempt = 0; attempt < 2; ++attempt)
    {
        const auto before = receivedPacketCounts();
        if (!before || (before->received == consumedRxCounts.received &&
                        before->loraCrcErrors == consumedRxCounts.loraCrcErrors))
        {
            return;
        }

        bool valid = true;
        int8_t rssi = 0;
        if (lastRadioParameters.frequency->mode == GATAS::Modulation::LORA)
        {
            sx126x_irq_mask_t raw = 0;
            sx126x_get_irq_status(this, &raw);
            // Retain the error from the task's acknowledged IRQ and check for
            // another error arriving since then. A latched CRC error can cover
            // multiple coalesced packets; reject conservatively in that case.
            const bool loraCrcError = pendingLoraCrcError || (raw & SX126X_IRQ_CRC_ERROR);
            valid = !loraCrcError;
            sx126x_pkt_status_lora_t packet{};
            sx126x_get_lora_pkt_status(this, &packet);
            rssi = packet.signal_rssi_pkt_in_dbm;
        }
        else
        {
            sx126x_pkt_status_gfsk_t packet{};
            sx126x_get_gfsk_pkt_status(this, &packet);
            valid = valid && packet.rx_status.pkt_received && !packet.rx_status.abort_error &&
                    !packet.rx_status.length_error && !packet.rx_status.adrs_error;
            rssi = packet.rssi_avg;
        }

        sx126x_rx_buffer_status_t buffer{};
        sx126x_get_rx_buffer_status(this, &buffer);
        if (hasHalError())
        {
            return;
        }
        const uint8_t length = buffer.pld_len_in_bytes;
        valid = valid && length >= 4 &&
                (!lastRadioParameters.config->manchester || length % 2 == 0) &&
                (lastRadioParameters.config->protocol() != GATAS::ConspicuityProtocol::ADSL_O_BAND_HDR || length % 4 == 0);
        auto data = valid ? static_cast<uint8_t *>(getGlobalPool().alloc(length)) : nullptr;
        GATAS::DataFrame frame{
            .epochSeconds = CoreUtils::secondsSinceEpoch(),
            .frequency = lastRadioParameters.hopFrequency,
            .config = lastRadioParameters.config,
            .frame = {getGlobalPool(), data},
            .length = length,
            .rssidBm = rssi};
        if (data != nullptr)
        {
            // Continuous RX advances this pointer and wraps through the 256-byte
            // buffer. The configured RX base is only the first packet's address.
            sx126x_read_buffer(this, buffer.buffer_start_pointer, data, length);
        }
        const auto after = receivedPacketCounts();
        if (!after || hasHalError())
        {
            return;
        }
        if (before->received != after->received || before->loraCrcErrors != after->loraCrcErrors)
        {
            continue;
        }
        consumedRxCounts = *after;
        pendingLoraCrcError = false;
        if (data != nullptr)
        {
            ++statistics.receivedPackets;
            rxDataFrameQueue->push(etl::move(frame));
        }
        return;
    }
    // A completion may have coalesced with the IRQ already acknowledged by the
    // task. Preserve work explicitly instead of relying on another rising edge.
    pendingRxRead = true;
    xTaskNotify(taskHandle, TaskState::DIO1_EVENT, eSetBits);
}

bool Sx1262::waitBusy(uint16_t minimumDelay) const
{
    GATAS_ASSERT(minimumDelay > 0, "Must be > 0");
    constexpr uint16_t checkInterval = 100; // loop cycles per check

    uint8_t countdown = checkInterval;
    vTaskDelay(TASK_DELAY_MS(5));
    while (gpio_get(busyPin))
    {
        if (--countdown == 0)
        {
            statistics.buzyWaitsTimeout += 1;
            return false;
        }
    }

    vTaskDelay(TASK_DELAY_MS(minimumDelay));
    return true;
}

AbstractTransceiver::RadioStatus Sx1262::standBy(Standby mode)
{
    GATAS_MEASURE("standBy", 500);
    return sx126x_set_standby(this, mode == Standby::RC ? SX126X_STANDBY_CFG_RC : SX126X_STANDBY_CFG_XOSC) == SX126X_STATUS_OK ? RadioStatus::OK : RadioStatus::ERROR;
}

etl::optional<AbstractTransceiver::Irq> Sx1262::readIrq()
{
    sx126x_irq_mask_t raw = 0;
    if (sx126x_get_irq_status(this, &raw) != SX126X_STATUS_OK) { return etl::nullopt; }
    uint8_t events = 0;
    if (raw & SX126X_IRQ_RX_DONE)
    {
        pendingLoraCrcError = lastRadioParameters.frequency->mode == GATAS::Modulation::LORA &&
                              (raw & SX126X_IRQ_CRC_ERROR);
    }
    if ((raw & SX126X_IRQ_RX_DONE) || pendingRxRead)
    {
        events |= IRQ_RX_DONE;
    }
    if (raw & SX126X_IRQ_TX_DONE) { events |= IRQ_TX_DONE; }
    if (raw & SX126X_IRQ_TIMEOUT) { events |= IRQ_TIMEOUT; }
    return Irq{raw, events};
}

AbstractTransceiver::RadioStatus Sx1262::acknowledgeIrq(uint32_t raw)
{
    return sx126x_clear_irq_status(this, raw) == SX126X_STATUS_OK ? RadioStatus::OK : RadioStatus::ERROR;
}

void Sx1262::checkAndClearDeviceErrors()
{
    sx126x_status_t status = sx126x_get_device_errors(this, &currentDeviceError);
    if (status == SX126X_STATUS_OK && currentDeviceError != 0)
    {
        statistics.deviceErrors += 1;
        lastDeviceError = currentDeviceError;
        sx126x_clear_device_errors(this);
        GATAS_WARN("Device Error: %b", currentDeviceError);
    }
}

bool Sx1262::initializeRadio()
{
    char data[7]{};
    if (sx126x_read_register(this, 0x0320, reinterpret_cast<uint8_t *>(data), 6) != SX126X_STATUS_OK || strncmp(data, "SX126", 5) != 0)
    {
        return false;
    }
    radioInit();
    return !hasHalError();
}
