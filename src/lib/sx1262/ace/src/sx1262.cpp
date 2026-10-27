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

void Sx1262::start()
{
    getBus().subscribe(*this);
};

GATAS::PostConstruct Sx1262::postConstruct()
{
    spiHall = static_cast<SpiModule *>(BaseModule::moduleByName(*this, SpiModule::NAMES[device]));

    if (spiHall == nullptr)
    {
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }

    rxDataFrameQueue = static_cast<RxDataFrameQueue *>(BaseModule::moduleByName(*this, RxDataFrameQueue::NAME));
    if (rxDataFrameQueue == nullptr)
    {
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(csPin);
    gpio_set_dir(csPin, GPIO_OUT);
    gpio_put(csPin, 1);

    // Busy Pin has PullUp from SX1262 so kept as input only
    gpio_init(busyPin);
    gpio_set_dir(busyPin, GPIO_IN);
    gpio_pull_up(busyPin);

    gpio_init(dio1Pin);
    gpio_set_dir(dio1Pin, GPIO_IN);
    gpio_pull_up(dio1Pin);

    // Read the device type
    char data[7];
    sx126x_read_register(this, 0x0320, (uint8_t *)data, 6);
    data[6] = 0;

    // Note that even for a SX1262, the version might come back as SX1261
    // https://forum.lora-developers.semtech.com/t/sx126x-device-id/1508
    if (strncmp(data, "SX126", 5) != 0)
    {
        GATAS_WARN("Expected SX126X, but found [%s] ", data);
        return GATAS::PostConstruct::HARDWARE_NOT_FOUND;
    }
    GATAS_INFO(" found [%s] (Sx1261 is normal for a Sx1262) ", data);

    radioInit();

    if (xTaskCreate(sx1262Trampoline, NAMES[radioNo].cbegin(), configMINIMAL_STACK_SIZE + 256, this, tskIDLE_PRIORITY + 4, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }
    registerPinInterrupt(dio1Pin, GPIO_IRQ_EDGE_RISE, taskHandle, 0);

    GATAS_INFO("Initialised on cs:%d busy:%d dio1:%d ", csPin, busyPin, dio1Pin);

    // Make the SPI pins available to picotool
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(csPin), NAMES[radioNo].cbegin()));
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(busyPin), NAMES[radioNo].cbegin()));
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(dio1Pin), NAMES[radioNo].cbegin()));

    return GATAS::PostConstruct::OK;
}

void Sx1262::enterDisabledState(uint8_t radioNo, const Configuration &config)
{
    auto pinMap = config.pinMap(NAMES[radioNo]);
    auto csPin = pinMap.at(GATAS::PinType::CS);
    gpio_init(csPin);
    gpio_set_dir(csPin, GPIO_OUT);
    gpio_put(csPin, 1);
}

void Sx1262::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::min(static_cast<int8_t>(22), lastRadioParameters.frequency->powerdBm);
    stream << "{";
    stream << "\"deviceErrors\":" << statistics.deviceErrors;
    stream << ",\"spiNo\":" << spiHall->spiNum();
    stream << ",\"receivedPackets:k\":" << statistics.receivedPackets;
    stream << ",\"transmittedPackets:k\":" << statistics.transmittedPackets;
    stream << ",\"buzyWaitsTimeout:err\":" << statistics.buzyWaitsTimeout;
    stream << ",\"txQueueFull:err\":" << statistics.queueFull;
    stream << ",\"txQueueSize:k\":" << txQueue.size();
    stream << ",\"txConfigureStarted:k\":" << statistics.txConfigureStarted;
    stream << ",\"txConfigureCompleted:k\":" << statistics.txConfigureCompleted;
    stream << ",\"txSendPacketCompleted:k\":" << statistics.txSendPacketCompleted;
    stream << ",\"txTimeout:k\":" << statistics.txTimeout;
    stream << ",\"rxTimeout:k\":" << statistics.rxTimeout;
    stream << ",\"unexpectedTimeout:err\":" << statistics.unexpectedTimeout;
    stream << ",\"txRecovery:k\":" << statistics.txRecovery;
    stream << ",\"halBusyTimeout:err\":" << statistics.halBusyTimeout;
    stream << ",\"halSpiErrors:err\":" << statistics.halSpiErrors;
    stream << ",\"mode\":" << "\"" << GATAS::modulationToString(lastRadioParameters.frequency->mode) << "\"";
    stream << ",\"dataSource\":" << "\"" << GATAS::toString(lastRadioParameters.config->dataSource()) << "\"";
    stream << ",\"frequency:hz\":" << lastRadioParameters.hopFrequency;
    stream << ",\"powerdBm:dbm\":" << powerdBm;
    stream << ",\"radio\":" << radioNo;
    stream << ",\"txEnabled:b\":" << txEnabled;
    stream << ",\"hasGpsFix:b\":" << hasGpsFix;
    stream << ",\"lastDeviceError:bin\":" << lastDeviceError;
    stream << ",\"currentDeviceError:bin\":" << currentDeviceError;
    stream << "}";
}

void Sx1262::on_receive(const GATAS::RadioTxFrameMsg &msg)
{
    if (msg.radioNo == radioNo)
    {
        if (hasGpsFix && txEnabled)
        {
            TxPacket txPacket{
                .radioParameters = msg.radioParameters,
                .frame = etl::move(msg.frame),
                .length = msg.length};

            if (!txQueue.push(etl::move(txPacket)))
            {
                statistics.queueFull += 1;
            }
        }

        xTaskNotify(taskHandle, TaskState::TX_PENDING, eSetBits);
    }
}

void Sx1262::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == Sx1262::NAMES[radioNo])
    {
        txEnabled = msg.config.valueByPath(true, Sx1262::NAMES[radioNo], "txEnabled");
        offsetHz = msg.config.valueByPath(true, Sx1262::NAMES[radioNo], "offset");
    }
    groundStation = msg.config.gaTasConfig().conspicuity.groundStation;
}

void Sx1262::on_receive(const GATAS::GpsStatsMsg &msg)
{
    hasGpsFix = msg.gpsStats.gpsFix.hasFix;
}

void Sx1262::on_receive(const GATAS::RadioControlMsg &msg)
{
    if (msg.radioNo == radioNo)
    {
        newRxRadioParameters = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), msg.radioParameters);
        xTaskNotify(taskHandle, TaskState::RX_CONFIG_PENDING, eSetBits);
    }
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
    standBy(SX126X_STANDBY_CFG_RC);

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

void Sx1262::configureSx1262(const GATAS::RadioParameters &newParameters,
                             RadioDirection direction, uint8_t payloadLength)
{
    GATAS_ASSERT(newParameters.config != nullptr && newParameters.frequency != nullptr, "Invalid radio parameters");
    GATAS_ASSERT(newParameters.config->dataSource() != GATAS::DataSource::NONE, "Cannot configure radio for NONE");
    GATAS_MEASURE("configureSx1262", 900);

    // Completion/timeout leaves the radio in standby; the task stops active RX.
    // Only a modem change requires selecting RC standby here.
    if (newParameters.frequency->mode != currentConfiguredModulation)
    {
        standBy(SX126X_STANDBY_CFG_RC);
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

    disablePinInterrupt(dio1Pin);
    sx126x_clear_irq_status(this, SX126X_IRQ_ALL);
    sx126x_set_dio_irq_params(this, SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    enablePinInterrupt(dio1Pin, DIO1_EVENT);

    sx126x_set_rx(this, SX126X_MAX_TIMEOUT_IN_MS);
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
    // 13.5.3 GetPacketStatus
    sx126x_pkt_status_gfsk_t pkt_status{};
    sx126x_get_gfsk_pkt_status(this, &pkt_status);
    if (hasHalError())
    {
        return;
    }
    if (pkt_status.rx_status.pkt_received && pkt_status.rx_status.abort_error == 0)
    {
        statistics.receivedPackets += 1;
        uint8_t receivedFrameLength = receivedPacketLength();

        if (receivedFrameLength >= 4)
        {
            // ADSL OHR has a lot of false packets, we eurly detect them and drop them
            if (lastRadioParameters.config->dataSource() == GATAS::DataSource::ADSLO_HDR && (receivedFrameLength % 4 != 0))
            {
                GATAS_WARN("Dropping invalid ADSL HDR packet");
                return;
            }

            if (auto frameData = static_cast<uint8_t *>(getGlobalPool().alloc(receivedFrameLength)))
            {
                GATAS::DataFrame rxFrame{
                    .epochSeconds = CoreUtils::secondsSinceEpoch(),
                    .frequency = lastRadioParameters.hopFrequency,
                    .config = lastRadioParameters.config,
                    .frame = {getGlobalPool(), frameData},
                    .length = receivedFrameLength,
                    .rssidBm = pkt_status.rssi_avg,
                };

                sx126x_read_buffer(this, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE, rxFrame.frame.get(), receivedFrameLength);
                if (!hasHalError())
                {
                    rxDataFrameQueue->push(etl::move(rxFrame));
                }
            }
        }
        else
        {
            GATAS_WARN("Incorrect frame length received %d", receivedFrameLength);
        }
    }
    else
    {
        // If we get here, there is some mis configuration going on
        checkAndClearDeviceErrors();
        GATAS_INFO("pkt_status.rx_status.pkt_received %d %d %d %d %d %d",
                   pkt_status.rx_status.pkt_received, pkt_status.rx_status.abort_error, pkt_status.rx_status.length_error,
                   pkt_status.rx_status.crc_error, pkt_status.rx_status.pkt_sent, pkt_status.rx_status.adrs_error);
    }
}

void Sx1262::receiveLORAPacket()
{
    // 13.5.3 GetPacketStatus
    sx126x_chip_status_t chip_status{};
    sx126x_get_status(this, &chip_status);
    if (hasHalError())
    {
        return;
    }
    if (chip_status.cmd_status == SX126X_CMD_STATUS_DATA_AVAILABLE)
    {
        sx126x_pkt_status_lora_t pkt_status{};
        sx126x_get_lora_pkt_status(this, &pkt_status);
        if (hasHalError())
        {
            return;
        }

        statistics.receivedPackets += 1;
        uint8_t receivedFrameLength = receivedPacketLength();

        if (receivedFrameLength >= 4)
        {
            if (auto frameData = static_cast<uint8_t *>(getGlobalPool().alloc(receivedFrameLength)))
            {
                GATAS::DataFrame rxFrame{
                    .epochSeconds = CoreUtils::secondsSinceEpoch(),
                    .frequency = lastRadioParameters.hopFrequency,
                    .config = lastRadioParameters.config,
                    .frame = {getGlobalPool(), frameData},
                    .length = receivedFrameLength,
                    .rssidBm = pkt_status.signal_rssi_pkt_in_dbm,
                };

                sx126x_read_buffer(this, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE, rxFrame.frame.get(), receivedFrameLength);
                if (!hasHalError())
                {
                    rxDataFrameQueue->push(etl::move(rxFrame));
                }
            }
        }
        else
        {
            GATAS_WARN("Incorrect frame length received %d", receivedFrameLength);
        }
    }
    else
    {
        // If we get here, there is some mis configuration going on
        checkAndClearDeviceErrors();
    }
}

uint8_t Sx1262::receivedPacketLength() const
{
    sx126x_rx_buffer_status_t rx_buffer_status{0, 0};
    sx126x_get_rx_buffer_status(this, &rx_buffer_status);
    return rx_buffer_status.pld_len_in_bytes;
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

void Sx1262::recordHalBusyTimeout()
{
    statistics.halBusyTimeout += 1;
    halError = true;
}

void Sx1262::recordHalSpiError()
{
    statistics.halSpiErrors += 1;
    halError = true;
}

sx126x_status_t Sx1262::standBy(sx126x_standby_cfg_t mode)
{
    GATAS_MEASURE("standBy", 500);

    // .715
    return sx126x_set_standby(this, mode);
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

sx126x_irq_mask_t Sx1262::getIrqStatus()
{
    sx126x_irq_mask_t mask = SX126X_IRQ_NONE;
    if (sx126x_get_irq_status(this, &mask) != SX126X_STATUS_OK)
    {
        return SX126X_IRQ_NONE;
    }
    return mask;
}

bool Sx1262::isTxDone()
{
    const sx126x_irq_mask_t mask = getIrqStatus();
    if (mask & SX126X_IRQ_TX_DONE)
    {
        sx126x_clear_irq_status(this, SX126X_IRQ_TX_DONE);
        return true;
    }
    return false;
}

sx126x_status_t Sx1262::sendPacket(const TxPacket &txPacket)
{
    // GATAS_INFO("Radio %d TX %s timeMs:%d", radioNo, GATAS::toString(command.txPacket.radioParameters.config->dataSource), CoreUtils::msInSecond());
    // TODO: Sometimes configuration can take a few msmeven we don't change protocol

    if (txPacket.radioParameters.frequency->mode == GATAS::Modulation::GFSK)
    {
        GATAS_MEASURE("sendGFSKPacket", 800);
        if (txPacket.radioParameters.config->manchester)
        {
            if (txPacket.length > GATAS::RADIO_MAX_TX_GFSK_FRAME_LENGTH)
            {
                GATAS_WARN("Frame too long for manchester encoding %d", txPacket.length);
                return SX126X_STATUS_UNKNOWN_VALUE;
            }
            uint8_t manchesterFrame[GATAS::RADIO_MAX_TX_GFSK_FRAME_LENGTH * MANCHESTER];
            manchesterEncode(manchesterFrame, txPacket.frame.get(), txPacket.length);
            sendPacket(txPacket.radioParameters, manchesterFrame, txPacket.length * MANCHESTER, 12);
            return SX126X_STATUS_OK;
        }
        else
        {
            sendPacket(txPacket.radioParameters, txPacket.frame.get(), txPacket.length, 12);
            return SX126X_STATUS_OK;
        }
    }
    else if (txPacket.radioParameters.frequency->mode == GATAS::Modulation::LORA)
    {
        GATAS_MEASURE("sendLORAPacket", 100);
        sendPacket(txPacket.radioParameters, txPacket.frame.get(), txPacket.length, 65);
        return SX126X_STATUS_OK;
    }
    return SX126X_STATUS_UNKNOWN_VALUE;
}

void Sx1262::sx1262Trampoline(void *arg)
{
    Sx1262 *sx1262 = static_cast<Sx1262 *>(arg);
    sx1262->sx1262Task(arg);
}

void Sx1262::sx1262Task(void *arg)
{
    (void)arg;
    constexpr uint32_t txDeadlineMs = 100;
    enum class Operation : uint8_t
    {
        IDLE,
        RX,
        TX
    };
    Operation operation = Operation::IDLE;
    bool doListen = false;
    bool recoveryPending = false;
    bool rxPacketPending = false;
    sx126x_irq_mask_t pendingAck = SX126X_IRQ_NONE;
    uint32_t txStartedAtMs = 0;

    // All helpers below run with the SPI lock held.
    const auto serviceIrq = [&]()
    {
        // Retain a failed acknowledgement to avoid counting its IRQ twice.
        if (pendingAck == SX126X_IRQ_NONE && !rxPacketPending)
        {
            sx126x_irq_mask_t irq = SX126X_IRQ_NONE;
            if (sx126x_get_irq_status(this, &irq) != SX126X_STATUS_OK)
            {
                return false;
            }
            const auto completedOperation = operation;
            if (irq & SX126X_IRQ_TIMEOUT)
            {
                if (completedOperation == Operation::TX)
                {
                    statistics.txTimeout += 1;
                }
                else if (completedOperation == Operation::RX)
                {
                    statistics.rxTimeout += 1;
                }
                else
                {
                    statistics.unexpectedTimeout += 1;
                }
                operation = Operation::IDLE;
                doListen = true;
            }
            if (irq & SX126X_IRQ_TX_DONE)
            {
                // TX_DONE alone confirms successful transmission.
                statistics.transmittedPackets += 1;
                operation = Operation::IDLE;
                doListen = true;
            }
            if ((irq & SX126X_IRQ_RX_DONE) && completedOperation == Operation::RX)
            {
                rxPacketPending = true;
                operation = Operation::IDLE;
                doListen = true;
            }
            pendingAck = irq;
        }
        if (pendingAck != SX126X_IRQ_NONE)
        {
            if (sx126x_clear_irq_status(this, pendingAck) != SX126X_STATUS_OK)
            {
                return false;
            }
            pendingAck = SX126X_IRQ_NONE;
        }
        if (rxPacketPending)
        {
            if (lastRadioParameters.frequency->mode == GATAS::Modulation::GFSK)
            {
                receiveGFSKPacket();
            }
            else if (lastRadioParameters.frequency->mode == GATAS::Modulation::LORA)
            {
                receiveLORAPacket();
            }
            rxPacketPending = false;
            return !hasHalError();
        }
        return true;
    };

    const auto startListening = [&]()
    {
        const auto &parameters = rxRadioParameters.config->dataSource() == GATAS::DataSource::NONE
                                     ? lastRadioParameters
                                     : rxRadioParameters;
        if (parameters.config->dataSource() == GATAS::DataSource::NONE)
        {
            operation = Operation::IDLE;
            doListen = false;
            return true;
        }
        configureSx1262(parameters, RadioDirection::RX, 0);
        if (hasHalError())
        {
            return false;
        }
        listen();
        operation = Operation::RX;
        doListen = hasHalError();
        return !doListen;
    };

    const auto recover = [&]()
    {
        clearHalError();
        recoveryPending = true;
        // NRESET is shared. Recover this device using standby, never radioInit.
        // Drain final IRQs before reconfiguration can clear them.
        if (standBy(SX126X_STANDBY_CFG_RC) != SX126X_STATUS_OK || !serviceIrq())
        {
            return;
        }
        operation = Operation::IDLE;
        currentConfiguredPcId = PROTOCOL_NONE.pcId;
        currentConfiguredModulation = GATAS::Modulation::NONE;
        doListen = true;
        recoveryPending = !startListening();
        if (!recoveryPending && !txQueue.empty())
        {
            // TX request bits may have coalesced before recovery. Schedule the
            // remaining real work; this is not a periodic retry or RX poll.
            xTaskNotify(taskHandle, TaskState::TX_PENDING, eSetBits);
        }
        // One bounded recovery attempt per wake. Persistent HAL failures remain
        // counted and are retried on the next notification, without polling.
    };

    while (true)
    {
        TickType_t waitTicks = portMAX_DELAY;
        if (operation == Operation::TX && !recoveryPending)
        {
            const uint32_t elapsed = CoreUtils::timeMs32() - txStartedAtMs;
            waitTicks = elapsed >= txDeadlineMs ? 0 : etl::max<TickType_t>(1, pdMS_TO_TICKS(txDeadlineMs - elapsed));
        }
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, waitTicks);
        if (notifyValue & TaskState::RX_CONFIG_PENDING)
        {
            rxRadioParameters = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), newRxRadioParameters);
            doListen = true;
        }

        bool locked;
        if (auto guard = spiHall->getLock(locked))
        {
            if (recoveryPending)
            {
                recover();
                continue;
            }

            clearHalError();
            // Stop RX before draining IRQs, keeping the oscillator running.
            // configureSx1262 selects RC standby if the modem must change.
            const bool stoppingRx = operation == Operation::RX && (doListen || !txQueue.empty());
            if (stoppingRx && standBy(SX126X_STANDBY_CFG_XOSC) != SX126X_STATUS_OK)
            {
                recover();
                continue;
            }

            const bool txExpired = operation == Operation::TX &&
                                   CoreUtils::timeMs32() - txStartedAtMs >= txDeadlineMs;
            if ((notifyValue & TaskState::DIO1_EVENT) || txExpired || stoppingRx ||
                pendingAck != SX126X_IRQ_NONE || rxPacketPending)
            {
                if (!serviceIrq())
                {
                    if (txExpired && operation == Operation::TX)
                    {
                        statistics.txRecovery += 1;
                        txQueue.clear();
                    }
                    recover();
                    continue;
                }
            }
            if (stoppingRx)
            {
                operation = Operation::IDLE;
            }
            if (operation == Operation::TX)
            {
                // Pending TX_DONE/TIMEOUT already finished TX above, even if
                // DIO1 was missed. Only an unconfirmed TX needs recovery.
                if (txExpired)
                {
                    statistics.txRecovery += 1;
                    txQueue.clear();
                    recover();
                }
                continue;
            }

            if (TxPacket txPacket; txQueue.pop(txPacket))
            {
                statistics.txConfigureStarted += 1;
                configureSx1262(txPacket.radioParameters, RadioDirection::TX, txPacket.length);
                if (hasHalError())
                {
                    recover();
                    continue;
                }
                statistics.txConfigureCompleted += 1;
                const auto status = sendPacket(txPacket);
                statistics.txSendPacketCompleted += 1;
                operation = status == SX126X_STATUS_OK ? Operation::TX : Operation::IDLE;
                txStartedAtMs = CoreUtils::timeMs32();
                if (status != SX126X_STATUS_OK || hasHalError())
                {
                    recover();
                }
                continue;
            }
            if (doListen && !startListening())
            {
                recover();
            }
        }
    }
}
