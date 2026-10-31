#include "../radiotunerrx_v2.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "../countryregulations_v2.hpp"

#include "etl/algorithm.h"
#include "etl/random.h"
#include "etl/utility.h"

#define GATAS_TUNE_LOG(fmt, ...) GATAS_LOG_IF(false, fmt, ##__VA_ARGS__)

GATAS::PostConstruct RadioTunerRx::postConstruct()
{
    if (moduleByName(*this, Radio::NAMES[0]))
    {
        radioCtxList.emplace_back(this, 0);
    }
    else
    {
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }
    if (moduleByName(*this, Radio::NAMES[1]))
    {
        radioCtxList.emplace_back(this, 1);
    }

    if (xTaskCreate(radioTuneTask, RadioTunerRx::NAME.cbegin(), configMINIMAL_STACK_SIZE + 384, this, tskIDLE_PRIORITY + 2, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }

    return GATAS::PostConstruct::OK;
}

void RadioTunerRx::start()
{
    getBus().subscribe(*this);
};

void RadioTunerRx::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"_dummy\": 0";
    for (const auto &taskCtx : radioCtxList)
    {
        taskCtx.getData(stream);
    }
    stream << ",\"zone\":\"" << currentZone.value().c_str() << "\"";
    stream << "}";
}

//*********************** Tuner tasks ***********************

void RadioTunerRx::radioTuneTask(void *arg)
{
    RadioTunerRx *radioTunerRx = static_cast<RadioTunerRx *>(arg);
    bool taskBlocked = false;
    uint16_t nextDelayMs = 200;
    uint32_t lastTrafficReEvalUs = CoreUtils::timeUs32() + RE_EVALUATE_DATASOURCES_USEC;

    while (true)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, TASK_DELAY_MS(nextDelayMs));

        if (notifyValue & TaskState::BLOCK)
        {
            taskBlocked = true;
            nextDelayMs = 100;
            radioTunerRx->eventSync.set(BIT_EVENT_DONE);
        }
        if (notifyValue & TaskState::UNBLOCK)
        {
            taskBlocked = false;
            radioTunerRx->eventSync.set(BIT_EVENT_DONE);
        }

        if (taskBlocked)
        {
            continue;
        }

        if (CoreUtils::isUsReached(lastTrafficReEvalUs))
        {
            lastTrafficReEvalUs = CoreUtils::timeUs32() + RE_EVALUATE_DATASOURCES_USEC;
            GATAS_INFO("Re-evaluating datasource assignment with traffic bias. Received packets");
            radioTunerRx->assignDataSourcesFromTask();
        }

        const uint32_t loopStartMs = CoreUtils::msSinceMidnight();
        uint32_t nextBoundaryInMs = std::numeric_limits<uint32_t>::max();

        for (auto &&ref : radioTunerRx->radioCtxList)
        {
            if (ref.protocolTimings.empty() || ref.maxTimeMs == 0)
            {
                continue;
            }

            // Position within the repeating cycle
            const int32_t cycleMs = loopStartMs % ref.maxTimeMs;

            // Find which entry covers this time
            size_t foundIdx = 0;
            for (size_t i = 0; i < ref.protocolTimings.size(); i++)
            {
                auto lenientCycleMs = cycleMs + 20;
                if (lenientCycleMs >= ref.protocolTimings[i].start() && lenientCycleMs < ref.protocolTimings[i].end())
                {
                    foundIdx = i;
                    break;
                }
            }

            //            GATAS_INFO("Radio %d cycleMs: %ld, ppsMS: %d  current entry: %u, start: %d, end: %d, ds: %s\n", ref.radioNo, cycleMs,  CoreUtils::msInSecond(), foundIdx, ref.protocolTimings[foundIdx].start, ref.protocolTimings[foundIdx].end, (ref.protocolTimings[foundIdx].protocolTimeConfig != nullptr) ? GATAS::toString(ref.protocolTimings[foundIdx].protocolTimeConfig->radioConfig.dataSource()) : "NOOP");
            const auto &entry = ref.protocolTimings[foundIdx];

            // Only reconfigure radio if protocol changed
            if (foundIdx != ref.currentProtocolIdx)
            {
                ref.currentProtocolIdx = foundIdx;
                if (entry.protocolTimeConfig != nullptr)
                {
                    // GATAS_TUNE_LOG("Radio %d switching to DS %s on channel %d", ref.radioNo, GATAS::toString(entry.protocolTimeConfig->radioConfig.dataSource()), static_cast<uint8_t>(entry.channel));
                    auto frequency = CountryRegulations::getFrequency(entry.protocolTimeConfig->rfConfig, entry.channel);
                    radioTunerRx->getBus().receive(GATAS::RadioControlMsg{
                        GATAS::RadioParameters(&entry.protocolTimeConfig->radioConfig, &entry.protocolTimeConfig->rfConfig, frequency, 0),
                        ref.radioNo});
                    ref.statistics.taskActivity += 1;
                }
            }

            // Time until this radio hits its next timing boundary.
            // We take the minimum across all radios so the task wakes on the earliest boundary.
            uint32_t timeToBoundary = static_cast<uint32_t>(entry.end() - cycleMs);
            if (timeToBoundary < nextBoundaryInMs)
            {
                nextBoundaryInMs = timeToBoundary;
            }
        }

        if (nextBoundaryInMs == std::numeric_limits<uint32_t>::max())
        {
            nextBoundaryInMs = 1000;
        }

        const uint32_t loopEndMs = CoreUtils::msSinceMidnight();
        const uint32_t loopElapsedMs = (loopEndMs >= loopStartMs)
                                           ? (loopEndMs - loopStartMs)
                                           : (86'400'000U - loopStartMs + loopEndMs);
        const int32_t correctedDelayMs = static_cast<int32_t>(nextBoundaryInMs) - static_cast<int32_t>(loopElapsedMs);
        nextDelayMs = static_cast<uint16_t>(etl::max(correctedDelayMs, static_cast<int32_t>(1)));
    }
}

// ******************** Message bus receive handlers ********************

void RadioTunerRx::on_receive(const GATAS::OwnshipPositionMsg &)
{
    const auto position = ownshipState.location.load();
    static auto lastTime = CoreUtils::timeUs32Raw();

    if (CoreUtils::isUsReachedRaw(lastTime) || currentZone.value() == CountryRegulations::Zone::ZONE0)
    {
        lastTime = CoreUtils::timeUs32Raw() + UPDATE_ZONE_REGULATION_EVERY;
        currentZone.set(CountryRegulations::zone(position.lat, position.lon));

        if (currentZone.isModified())
        {
            assignDataSourcesWithTrafficBias();
        }
    }
}

void RadioTunerRx::on_receive(const GATAS::IngressAircraftPositionMsg &msg)
{
    GATAS_ASSERT(msg.position.dataSource < GATAS::DataSource::_TRANSPROTOCOLS, "Invalid datasource");
    if (static_cast<uint8_t>(msg.position.dataSource) < slotReceive.size())
    {
        slotReceive[static_cast<uint8_t>(msg.position.dataSource)] += 1;
    }
}

void RadioTunerRx::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == Configuration::NAME)
    {
        const auto protocols = msg.config.gaTasConfig().protocols;
        if (!blockTasks())
        {
            return;
        }
        configuredDatasources = protocols;
        clearTrafficStats();
        assignDataSourcesFromTask();
        releaseTasks();
    }
}

void RadioTunerRx::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

uint8_t RadioTunerRx::hasReceived(GATAS::DataSource ds)
{
    GATAS_ASSERT(ds < GATAS::DataSource::_TRANSPROTOCOLS, "Invalid datasource");
    uint8_t priority = 0;
    const uint16_t receiveCount = slotReceive[static_cast<uint8_t>(ds)];
    if (receiveCount > 5) // We can properly optmise this by getting data frame the aircraft tracker
    {
        priority = 2;
    }
    else if (receiveCount > 0) // At least one aircraft
    {
        priority = 1;
    }
    slotReceive[static_cast<uint8_t>(ds)] = 0;
    return priority;
}

void RadioTunerRx::clearTrafficStats()
{
    etl::fill(slotReceive.begin(), slotReceive.end(), 0);
}

void RadioTunerRx::assignDataSourcesFromTask()
{
    etl::array<uint8_t, static_cast<uint8_t>(GATAS::DataSource::_TRANSPROTOCOLS)> receiveBoost = {};
    for (const auto &dsc : configuredDatasources)
    {
        receiveBoost[static_cast<uint8_t>(dsc.dataSource)] = hasReceived(dsc.dataSource);
    }
    assignDataSourcesImpl(etl::span<const uint8_t>(receiveBoost));
}

void RadioTunerRx::assignDataSourcesWithTrafficBias()
{
    if (!blockTasks())
    {
        return;
    }

    assignDataSourcesFromTask();
    releaseTasks();
}

void RadioTunerRx::assignDataSourcesImpl(etl::span<const uint8_t> receiveBoost)
{
    if (currentZone.value() == CountryRegulations::Zone::enum_type::ZONE0)
    {
        return;
    }

    auto availableTimings = CountryRegulations::getProtocolRxTimingsForZone(currentZone.value(), configuredDatasources);
    auto guard = BaseModule::lockSharedMutex();

    // Assign availableTimings evenly over the radios
    size_t radioCount = radioCtxList.size();
    size_t idx = 0;
    for (auto &ctx : radioCtxList)
    {
        ctx.clear();
        uint16_t secondIndex = 0;

        for (size_t i = 0; i < availableTimings.size(); i++)
        {
            if (i % radioCount == idx)
            {
                const auto *timing = availableTimings[i];
                const uint8_t extraCount = etl::min<uint8_t>(receiveBoost[static_cast<uint8_t>(timing->radioConfig.dataSource())], 2) + 1;
                for (uint8_t extra = 0; extra < extraCount; ++extra)
                {
                    appendProtocolTimings(ctx, timing, secondIndex);
                }
            }
        }

        // Shuffle
        // TODO: We can optmisr htis properly bynot caling this
        // when receiveBoost contains all, or near all protocols?
        spreadSecondIndex(ctx);

        // Fill NOOP gaps: prefer same-radio protocols first, then other radios
        for (auto &entry : ctx.protocolTimings)
        {
            if (entry.protocolTimeConfig != nullptr)
            {
                continue;
            }
            const uint16_t midMs = static_cast<uint16_t>((entry.start() + entry.end()) / 2);
            if (!fillNoop(entry, midMs, availableTimings, radioCount, idx, true))
            {
                fillNoop(entry, midMs, availableTimings, radioCount, idx, false);
            }
        }

        idx++;
    }
}

/**
 * Ensure that adjacent protcols are spread acros the whole schedule
 * effectivly this means that if OGN was received, we ensure that OGN os not listend to twwice after eachother, but spread more evenly.
 * Example OGN, FANET, OGN, ADSL,  instead of OGN, OGN, FABET, ADSL
 * 
 * */
void RadioTunerRx::spreadSecondIndex(RadioProtocolCtx &ctx)
{
    using IndexVector = etl::vector<uint16_t, MAX_RX_TIMINGS>;

    IndexVector unique;

    // 1. Collect unique secondIndex values (stable order)
    for (const auto &e : ctx.protocolTimings)
    {
        bool found = false;
        for (const auto &v : unique)
        {
            if (v == e.secondIndex)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            unique.push_back(e.secondIndex);
        }
    }

    const uint16_t N = unique.size();
    if (N <= 2)
    {
        return; // nothing to improve
    }

    // 2. Build new order: even indices first, then odd
    IndexVector order;

    for (uint16_t i = 0; i < N; i += 2)
    {
        order.push_back(unique[i]);
    }

    for (uint16_t i = 1; i < N; i += 2)
    {
        order.push_back(unique[i]);
    }

    // 3. Rebuild vector in new order (stable within groups)
    TimeSlotVector result;

    for (uint16_t newIdx = 0; newIdx < order.size(); ++newIdx)
    {
        uint16_t oldIdx = order[newIdx];

        for (const auto &e : ctx.protocolTimings)
        {
            if (e.secondIndex == oldIdx)
            {
                RxTiming copy = e;
                copy.secondIndex = newIdx; // reassign to compact index
                result.push_back(copy);
            }
        }
    }

    // 4. Replace original
    ctx.protocolTimings = result;
}

bool RadioTunerRx::appendProtocolTimings(RadioProtocolCtx &ctx, const CountryRegulations::ProtocolRxTimeSlot *timing, uint16_t &secondIndex)
{
    if (timing == nullptr)
    {
        return false;
    }

    static constexpr uint16_t MAX_NOOP_MS = 200;
    static constexpr uint16_t SECOND_END = 1000;
    uint16_t prevRelEnd = 0;

    for (const auto &slot : timing->timeSlots)
    {
        const uint16_t slotRelStart = slot.start;
        while (prevRelEnd < slotRelStart && !ctx.protocolTimings.full())
        {
            const uint16_t fillRelEnd = etl::min(static_cast<uint16_t>(prevRelEnd + MAX_NOOP_MS), slotRelStart);
            ctx.protocolTimings.emplace_back(nullptr, CountryRegulations::Channel::NOOP, prevRelEnd, fillRelEnd, secondIndex);
            prevRelEnd = fillRelEnd;
        }
        if (!ctx.protocolTimings.full())
        {
            const uint16_t cappedRelEnd = etl::min(slot.end, SECOND_END);
            ctx.protocolTimings.emplace_back(timing, slot.channel, slotRelStart, cappedRelEnd, secondIndex);
            prevRelEnd = cappedRelEnd;
        }
    }

    while (prevRelEnd < SECOND_END && !ctx.protocolTimings.full())
    {
        const uint16_t fillRelEnd = etl::min(static_cast<uint16_t>(prevRelEnd + MAX_NOOP_MS), SECOND_END);
        ctx.protocolTimings.emplace_back(nullptr, CountryRegulations::Channel::NOOP, prevRelEnd, fillRelEnd, secondIndex);
        prevRelEnd = fillRelEnd;
    }

    ++secondIndex;
    ctx.maxTimeMs = static_cast<uint32_t>(secondIndex) * 1000U;
    return !ctx.protocolTimings.full();
}

bool RadioTunerRx::fillNoop(RxTiming &entry, uint16_t midMs, etl::span<const CountryRegulations::ProtocolRxTimeSlot *> timings, size_t radioCount, size_t radioIdx, bool sameRadio)
{
    for (size_t si = 0; si < timings.size(); si++)
    {
        bool isOwnRadio = (si % radioCount == radioIdx);
        if (isOwnRadio != sameRadio)
        {
            continue;
        }
        const auto *fitting = CountryRegulations::findFittingTiming(midMs, timings[si]->timeSlots);
        if (fitting != nullptr)
        {
            entry.protocolTimeConfig = timings[si];
            entry.channel = fitting->channel;
            return true;
        }
    }
    return false;
}

bool RadioTunerRx::blockTasks()
{
    eventSync.clear(BIT_EVENT_DONE);
    xTaskNotify(taskHandle, TaskState::BLOCK, eSetBits);

    if (!eventSync.wait(BIT_EVENT_DONE, pdMS_TO_TICKS(2000)))
    {
        GATAS_WARN("Failed to wait for event sync");
        // Cancel a pause that may still be pending on a delayed worker.
        releaseTasks();
        return false;
    }
    return true;
}

void RadioTunerRx::releaseTasks()
{
    xTaskNotify(taskHandle, TaskState::UNBLOCK, eSetBits);
}
