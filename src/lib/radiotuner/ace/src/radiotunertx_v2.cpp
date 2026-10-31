#include "../radiotunertx_v2.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "../countryregulations_v2.hpp"

#include "etl/algorithm.h"
#include "ace/measure.hpp"

GATAS::PostConstruct RadioTunerTx::postConstruct()
{
    if (xTaskCreate(radioTuneTaskTrampoline, RadioTunerTx::NAME.cbegin(), configMINIMAL_STACK_SIZE + 256, this, tskIDLE_PRIORITY + 2, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }

    return GATAS::PostConstruct::OK;
}

void RadioTunerTx::start()
{
    getBus().subscribe(*this);

    Configuration *configuration = static_cast<Configuration *>(BaseModule::moduleByName(*this, Configuration::NAME));
    if (configuration)
    {
        on_receive(GATAS::ConfigUpdatedMsg(*configuration, Configuration::NAME));
    }
};

void RadioTunerTx::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"_dummy\": 0";
    stream << ",\"zone\":\"ZONE" << static_cast<uint8_t>(currentZone) << "\"";
    stream << ",\"taskActivity:k\":" << statistics.taskActivity;

    stream << ",\"schedule:rtx\":[";
    bool firstDs = true;
    for (const auto &ds : dataSourceTxEvents)
    {
        if (!firstDs)
        {
            stream << ",";
        }
        firstDs = false;
        const char *dsName = GATAS::toString(ds.slot->radioConfig.dataSource());

//#if GATAS_DEBUG == 1
//         uint16_t minT = ds.slot->txMinTime;
//         uint16_t maxT = ds.slot->txMaxTime;
// #else

        uint16_t minT = isAirborne ? ds.slot->txMinTime : ds.slot->reducedTxMinTime;
        uint16_t maxT = isAirborne ? ds.slot->txMaxTime : ds.slot->reducedTxMaxTime;
// #endif
        stream << "{\"ds\":\"" << dsName << "\",\"min\":" << minT << ",\"max\":" << maxT << ",\"slots\":[";
        bool firstSlot = true;
        for (const auto &ts : ds.slot->timeSlots)
        {
            if (!firstSlot)
            {
                stream << ",";
            }
            firstSlot = false;
            stream << "{\"s\":" << ts.start << ",\"e\":" << ts.end << ",\"ch\":" << static_cast<uint8_t>(ts.channel) << "}";
        }
        stream << "]}";
    }
    stream << "]";

    stream << "}";
}

//*********************** Tuner tasks ***********************

void RadioTunerTx::radioTuneTaskTrampoline(void *arg)
{
    RadioTunerTx *radioTunerRx = static_cast<RadioTunerTx *>(arg);
    radioTunerRx->radioTuneTask();
}

void RadioTunerTx::radioTuneTask()
{
    int32_t nextDelayMs = 200;
    bool taskBlock = false;
    while (true)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, TASK_DELAY_MS(nextDelayMs));

        if (notifyValue & TaskState::BLOCK)
        {
            taskBlock = true;
            // Disable the transceivers during reconfiguration.
            // RadioTunerRx takes care of the actual radio configuration.
            eventSync.set(BIT_EVENT_DONE);
        }
        // A timeout can queue UNBLOCK before a delayed BLOCK is consumed.
        // Match RX: release must win when both notification bits are present.
        if (notifyValue & TaskState::UNBLOCK)
        {
            taskBlock = false;
            eventSync.set(BIT_EVENT_DONE);
        }

        if (!taskBlock)
        {
            for (auto &&ds : dataSourceTxEvents)
            {
                if (CoreUtils::isUsReached(ds.atTime))
                {
                    auto channelTiming = CountryRegulations::findFittingTiming(CoreUtils::timeMs32(), ds.slot->timeSlots);
                    if (channelTiming == nullptr)
                    {
                        continue;
                    }

                    auto frequencyHz = CountryRegulations::getFrequency(ds.slot->rfConfig, channelTiming->channel);
                    
                    // GATAS_INFO("TX: DS: %s Freq:%lu radio:%d id:%u ms:%lu", GATAS::toString(ds.slot->radioConfig.dataSource()), frequencyHz, dataSourceToRadio[static_cast<uint8_t>(ds.slot->radioConfig.dataSource())], channelTiming->id, CoreUtils::timeMs32());
                    
                    GATAS_MEASURE("Request TX", 2000);
                    getBus().receive(
                        GATAS::RadioTxPositionRequestMsg{
                            GATAS::RadioParameters{
                                &ds.slot->radioConfig,
                                &ds.slot->rfConfig,
                                frequencyHz,
                                channelTiming->id},
                            dataSourceToRadio[static_cast<uint8_t>(ds.slot->radioConfig.dataSource())]});
                    statistics.taskActivity += 1;
// #if GATAS_DEBUG == 1
                    // auto delayMs = CountryRegulations::nextRandomTxTime(false, *ds.slot);
// #else
                    auto delayMs = CountryRegulations::nextRandomTxTime(!isAirborne, *ds.slot);
// #endif
                    auto currentTimeUs = CoreUtils::timeUs32();
                    if (delayMs != UINT32_MAX)
                    {
                        ds.atTime = currentTimeUs + delayMs * 1000;
                    }
                    else
                    {
                        GATAS_WARN("Warning: Next random no timing found %s ", GATAS::toString(ds.slot->radioConfig.dataSource()));
                        ds.atTime = currentTimeUs + 950'000;
                    }
                }
            }

            // Decide the protocol that should be send next
            auto currentTimeUs = CoreUtils::timeUs32();
            int32_t nextUpIn = 2'000'000;
            for (auto &&ds : dataSourceTxEvents)
            {
                auto toRef = CoreUtils::usToReference(ds.atTime, currentTimeUs);
                if (toRef < nextUpIn)
                {
                    nextUpIn = toRef;
                }
            }

            if (nextUpIn < 1000)
            {
                nextUpIn = 1000;
            }
            // clamp to max 1 second in case datasources was empty
            nextDelayMs = nextUpIn / 1000;
        }
    }
}

// ******************** Message bus receive handlers ********************

void RadioTunerTx::on_receive(const GATAS::OwnshipPositionMsg &)
{
    const auto position = ownshipState.location.load();
    static auto lastTime = CoreUtils::timeUs32Raw();
    // Update ZONE every 30 seconds, or when still at ZONE0
    isAirborne = position.groundSpeed >= GATAS::GROUNDSPEED_CONSIDERING_AIRBORN;
    if (CoreUtils::isUsReachedRaw(lastTime) || currentZone == CountryRegulations::Zone::ZONE0)
    {
        lastTime = CoreUtils::timeUs32Raw() + UPDATE_ZONE_REGULATION_EVERY;
        auto newZone = CountryRegulations::zone(position.lat, position.lon);
        if (newZone != currentZone)
        {
            currentZone = newZone;
            assignDataSources(configuredDatasources);
        }
    }
}

void RadioTunerTx::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == Configuration::NAME)
    {
        auto gaTasConfiguration = msg.config.gaTasConfig();
        assignDataSources(gaTasConfiguration.protocols);
    }
}

void RadioTunerTx::on_receive(const GATAS::RadioControlMsg &msg)
{
    uint8_t dsId = static_cast<uint8_t>(msg.radioParameters.config->dataSource());
    if (dsId < static_cast<uint8_t>(GATAS::DataSource::_TRANSPROTOCOLS))
    {
        dataSourceToRadio[dsId] = msg.radioNo;
    }
    else
    {
        GATAS_WARN("DS: %d ", dsId);
    }
}

void RadioTunerTx::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void RadioTunerTx::assignDataSources(const etl::span<GATAS::DataSourceConfig> &newDataSources)
{
    auto guard = BaseModule::lockSharedMutex();
    eventSync.clear(BIT_EVENT_DONE);
    xTaskNotify(taskHandle, TaskState::BLOCK, eSetBits);

    // Expetced is 200ms per tick, we wait 10 times as long, much much longer
    if (!eventSync.wait(BIT_EVENT_DONE, pdMS_TO_TICKS(200 * 10)))
    {
        GATAS_INFO("RadioTunerTx: Failed to wait for event sync");
        xTaskNotify(taskHandle, TaskState::UNBLOCK, eSetBits);
        return;
    }

    if (auto guard = SpinlockGuard(CoreUtils::sharedSpinLock()))
    {
        configuredDatasources.clear();
        dataSourceTxEvents.clear();

        configuredDatasources.insert(configuredDatasources.end(), newDataSources.begin(), newDataSources.end());
        for (auto &&ds : newDataSources)
        {
            // Ignore Datasources that are not for transmission
            if (!ds.isTx())
            {
                continue;
            }
            const auto timing = CountryRegulations::getProtocolTxTimings(currentZone, ds.dataSource);
            
            if (!timing.empty())
            {
                for (const auto &entry : timing)
                {
                    if (dataSourceTxEvents.full())
                    {
                        GATAS_WARN("RadioTunerTx: TX schedule full, ignoring %s",
                                   GATAS::toString(ds.dataSource));
                        break;
                    }
                    dataSourceTxEvents.emplace_back(DataSourceTxEvent{&entry, CoreUtils::timeUs32()});
                }
            }
        }
    }

    xTaskNotify(taskHandle, TaskState::UNBLOCK, eSetBits);
}
