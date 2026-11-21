#include "../abstracttransceiver.hpp"
#include "ace/manchester.hpp"
#include "ace/measure.hpp"
#include "pico/binary_info.h"
#include <iterator>

void AbstractTransceiver::start()
{
    getBus().subscribe(*this);
}

GATAS::PostConstruct AbstractTransceiver::postConstruct()
{
    if (schedulerOnly)
    {
        GATAS_INFO("Scheduler-only mode for %s", moduleName.cbegin());
        return GATAS::PostConstruct::OK;
    }

    if (csPin >= NUM_BANK0_GPIOS || busyPin >= NUM_BANK0_GPIOS || dio1Pin >= NUM_BANK0_GPIOS ||
        (resetPin != UINT8_MAX && (resetPin >= NUM_BANK0_GPIOS || resetPin == csPin || resetPin == busyPin || resetPin == dio1Pin)) ||
        device >= SpiModule::MAX_SPI_MODULES || csPin == busyPin || csPin == dio1Pin || busyPin == dio1Pin)
    {
        return GATAS::PostConstruct::HARDWARE_NOT_CONFIGURED;
    }
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
    gpio_put(csPin, 1);
    gpio_set_dir(csPin, GPIO_OUT);

    // Busy and IRQ are driven by the radio.
    gpio_init(busyPin);
    gpio_set_dir(busyPin, GPIO_IN);
    gpio_pull_up(busyPin);

    gpio_init(dio1Pin);
    gpio_set_dir(dio1Pin, GPIO_IN);
    gpio_pull_up(dio1Pin);

    if (!initializeRadio() || hasHalError())
    {
        return GATAS::PostConstruct::HARDWARE_NOT_FOUND;
    }

    if (xTaskCreate(taskTrampoline, moduleName.cbegin(), configMINIMAL_STACK_SIZE + 256, this, tskIDLE_PRIORITY + 4, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }
    registerPinInterrupt(dio1Pin, GPIO_IRQ_EDGE_RISE, taskHandle, 0);

    GATAS_INFO("Initialised on cs:%d busy:%d dio1:%d ", csPin, busyPin, dio1Pin);

    // Make the SPI pins available to picotool
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(csPin), moduleName.cbegin()));
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(busyPin), moduleName.cbegin()));
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(dio1Pin), moduleName.cbegin()));

    return GATAS::PostConstruct::OK;
}

void AbstractTransceiver::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::min(static_cast<int8_t>(22), lastRadioParameters.frequency->powerdBm);
    stream << "{";
    stream << "\"deviceErrors\":" << statistics.deviceErrors;
    if (spiHall != nullptr)
    {
        stream << ",\"spiNo\":" << spiHall->spiNum();
    }
    stream << ",\"schedulerOnly:b\":" << schedulerOnly;
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
    stream << ",\"halBusyTimeout:err\":" << halBusyTimeout;
    stream << ",\"halSpiErrors:err\":" << halSpiErrors;
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

AbstractTransceiver::RadioStatus AbstractTransceiver::sendQueuedPacket(const TxPacket &txPacket)
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
                return RadioStatus::ERROR;
            }
            uint8_t manchesterFrame[GATAS::RADIO_MAX_TX_GFSK_FRAME_LENGTH * MANCHESTER];
            manchesterEncode(manchesterFrame, txPacket.frame.get(), txPacket.length);
            sendPacket(txPacket.radioParameters, manchesterFrame, txPacket.length * MANCHESTER, 12);
            return RadioStatus::OK;
        }
        else
        {
            sendPacket(txPacket.radioParameters, txPacket.frame.get(), txPacket.length, 12);
            return RadioStatus::OK;
        }
    }
    else if (txPacket.radioParameters.frequency->mode == GATAS::Modulation::LORA)
    {
        GATAS_MEASURE("sendLORAPacket", 100);
        sendPacket(txPacket.radioParameters, txPacket.frame.get(), txPacket.length, 65);
        return RadioStatus::OK;
    }
    return RadioStatus::ERROR;
}

void AbstractTransceiver::taskTrampoline(void *arg)
{
    AbstractTransceiver *sx1262 = static_cast<AbstractTransceiver *>(arg);
    sx1262->radioTask(arg);
}

void AbstractTransceiver::radioTask(void *arg)
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
    uint32_t pendingAck = 0;
    uint32_t txStartedAtMs = 0;

    // All helpers below run with the SPI lock held.
    const auto serviceIrq = [&]()
    {
        // Retain a failed acknowledgement to avoid counting its IRQ twice.
        if (pendingAck == 0 && !rxPacketPending)
        {
            auto result = readIrq();
            if (!result)
            {
                return false;
            }
            const auto irq = result->events;
            const auto completedOperation = operation;
            if (irq & IRQ_TIMEOUT)
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
            if (irq & IRQ_TX_DONE)
            {
                // TX_DONE alone confirms successful transmission.
                statistics.transmittedPackets += 1;
                operation = Operation::IDLE;
                doListen = true;
            }
            if ((irq & IRQ_RX_DONE) && completedOperation == Operation::RX)
            {
                rxPacketPending = true;
                operation = Operation::IDLE;
                doListen = true;
            }
            pendingAck = result->raw;
        }
        if (pendingAck != 0)
        {
            if (acknowledgeIrq(pendingAck) != RadioStatus::OK)
            {
                return false;
            }
            pendingAck = 0;
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
            else if (lastRadioParameters.frequency->mode == GATAS::Modulation::OOK)
            {
                receiveOOKPacket();
            }
            rxPacketPending = false;
            return !hasHalError();
        }
        return true;
    };

    const auto startListening = [&]()
    {
        if (rxRadioParameters.config == nullptr)
        {
            operation = Operation::IDLE;
            doListen = false;
            return true;
        }
        const auto &parameters = rxRadioParameters.config->dataSource() == GATAS::DataSource::NONE
                                     ? lastRadioParameters
                                     : rxRadioParameters;
        if (parameters.config->dataSource() == GATAS::DataSource::NONE ||
            !supportsRx(parameters.frequency->capability))
        {
            operation = Operation::IDLE;
            doListen = false;
            return true;
        }
        configureRadio(parameters, RadioDirection::RX, 0);
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
        if (standBy(Standby::RC) != RadioStatus::OK || !serviceIrq())
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
            rxRadioParameters = newRxRadioParameters.load();
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
            // configureRadio selects RC standby if the modem must change.
            const bool stoppingRx = operation == Operation::RX && (doListen || !txQueue.empty());
            if (stoppingRx && standBy(Standby::XOSC) != RadioStatus::OK)
            {
                recover();
                continue;
            }

            const bool txExpired = operation == Operation::TX &&
                                   CoreUtils::timeMs32() - txStartedAtMs >= txDeadlineMs;
            if ((notifyValue & TaskState::DIO1_EVENT) || txExpired || stoppingRx ||
                pendingAck != 0 || rxPacketPending)
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
                configureRadio(txPacket.radioParameters, RadioDirection::TX, txPacket.length);
                if (hasHalError())
                {
                    recover();
                    continue;
                }
                statistics.txConfigureCompleted += 1;
                const auto status = sendQueuedPacket(txPacket);
                statistics.txSendPacketCompleted += 1;
                operation = status == RadioStatus::OK ? Operation::TX : Operation::IDLE;
                txStartedAtMs = CoreUtils::timeMs32();
                if (status != RadioStatus::OK || hasHalError())
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

uint8_t AbstractTransceiver::readCapabilities(const Configuration &config, etl::string_view name,
                                              uint8_t supported)
{
    using GATAS::RadioCapability;
    static constexpr GATAS::Mapping<RadioCapability, etl::string_view> capabilities[] = {
        {RadioCapability::Gfsk868, "Gfsk868"},
        {RadioCapability::Lora868, "Lora868"},
        {RadioCapability::Gfsk915, "Gfsk915"},
        {RadioCapability::Ook1090, "Ook1090"},
    };
    const auto configured = config.strValueByPath("", name, "capabilities");
    etl::string_view remaining(configured.data(), configured.size());
    uint8_t mask = 0;
    while (!remaining.empty())
    {
        const auto separator = remaining.find(',');
        const auto field = remaining.substr(0, separator);
        const auto start = field.find_first_not_of(" \t");
        const auto end = field.find_last_not_of(" \t");
        const auto value = start == etl::string_view::npos ? etl::string_view{} : field.substr(start, end - start + 1);
        const auto *entry = etl::find_if(std::begin(capabilities), std::end(capabilities),
                                        [value](const auto &item) { return item.name == value; });
        if (configured.is_truncated() || entry == std::end(capabilities) ||
            (supported & static_cast<uint8_t>(entry->type)) == 0)
        {
            GATAS_WARN("Invalid radio capabilities '%s' for %.*s", configured.c_str(),
                       static_cast<int>(name.size()), name.data());
            return 0;
        }
        mask |= static_cast<uint8_t>(entry->type);
        if (separator == etl::string_view::npos)
        {
            break;
        }
        remaining.remove_prefix(separator + 1);
    }
    return mask;
}

void AbstractTransceiver::on_receive(const GATAS::RadioControlMsg &msg)
{
    const auto &parameters = msg.radioParameters;
    const bool idle = parameters.config == nullptr && parameters.frequency == nullptr;
    if (!schedulerOnly && msg.radioNo == radioNo &&
        (idle || (parameters.config != nullptr && parameters.frequency != nullptr && supportsRx(parameters.frequency->capability))))
    {
        newRxRadioParameters.store(parameters);
        xTaskNotify(taskHandle, TaskState::RX_CONFIG_PENDING, eSetBits);
    }
}

void AbstractTransceiver::on_receive(const GATAS::GpsStatsMsg &msg)
{
    hasGpsFix = msg.gpsStats.gpsFix.hasFix;
}

void AbstractTransceiver::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == moduleName)
    {
        txEnabled = msg.config.valueByPath(txEnabled, moduleName, "txEnabled");
        offsetHz = msg.config.valueByPath(0, moduleName, "offset");
    }
    groundStation = msg.config.gaTasConfig().conspicuity.groundStation;
}

void AbstractTransceiver::on_receive(const GATAS::RadioTxFrameMsg &msg)
{
    if (!schedulerOnly && msg.radioNo == radioNo)
    {
        if (msg.radioParameters.config == nullptr || msg.radioParameters.frequency == nullptr ||
            !msg.frame || msg.length == 0 || msg.length > 255 ||
            (msg.radioParameters.config->manchester && msg.length > GATAS::RADIO_MAX_TX_GFSK_FRAME_LENGTH))
        {
            return;
        }
        if (hasGpsFix && supportsTx(msg.radioParameters.frequency->capability))
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
