#include "main.h"
#include "generated/build_time.hpp"
#include "generated/default_config.hpp"

/* System. */
#include <stdio.h>
#include <malloc.h>

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "croutine.h"
#include "task.h"

/* pico */
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/btstack_cyw43.h"
#include "pico/cyw43_driver.h"

/* Vendor. */
#include "etl/list.h"
#include "etl/error_handler.h"
#include "etl/exception.h"

/* GaTas. */
#include "ace/serialadsb.hpp"
#include "ace/dump1090client.hpp"

#include "ace/messagerouter.hpp"
#include "ace/constants.hpp"
#include "ace/aircrafttracker.hpp"
#include "ace/basemodule.hpp"
#include "ace/ownshipstate.hpp"
#include "ace/config.hpp"
#include "ace/inmemorystore.hpp"
#include "ace/flashstore.hpp"
#include "ace/ubloxm8n.hpp"
#include "ace/L76B.hpp"
#include "ace/StaticGPS.hpp"
#include "ace/adsbdecoder.hpp"
#include "ace/picortc.hpp"
#include "ace/wifiservice.hpp"
#include "ace/webserver.hpp"
#include "ace/gpsdecoder.hpp"
#include "ace/acespi.hpp"
#include "ace/bmp280.hpp"
#include "ace/sx1262.hpp"
#include "ace/radiotunerrx_v2.hpp"
#include "ace/rxdataframequeue.hpp"
#include "ace/radiotunertx_v2.hpp"
#include "ace/flarm2024.hpp"
#include "ace/ogn1.hpp"
#include "ace/adslace.hpp"
#include "ace/gdl90service.hpp"
#include "ace/gdloverudp.hpp"
#include "ace/dataport.hpp"
#include "ace/airconnect.hpp"
#include "ace/gatasconnect.hpp"
#include "ace/gatasconnectudp.hpp"
#include "ace/bluetooth.hpp"
#include "ace/fanetace.hpp"
#include "ace/idle.hpp"
#include "ace/manchester.hpp"

const char *GATAS_BUILD_TIMESTAMP = BUILD_TIMESTAMP;
const char *GATAS_BUILD_GIT_TAG = BUILD_GIT_TAG;
const uint32_t GATAS_BUILD_TIMESTAMP_EPOCH = BUILD_TIMESTAMP_EPOCH;

void etlcpp_receive_error(const etl::exception &e)
{
    printf("ETLCPP error was %s in file %s at line %d\n", e.what(), e.file_name(), e.line_number());
}

uint32_t getTotalHeap(void)
{
    extern char __StackLimit, __bss_end__;
    return &__StackLimit - &__bss_end__;
}

uint32_t getFreeHeap(void)
{
    auto m = mallinfo();
    return getTotalHeap() - m.uordblks;
}

void registerModules()
{
    BaseModule::registerModule(AceSpi::NAMES[0], true);
    BaseModule::registerModule(AceSpi::NAMES[1], true);
    BaseModule::registerModule(Bmp280::NAME, true);
    // BaseModule::registerModule(Config::NAME, true); // Uncomment if needed
    BaseModule::registerModule(Gdl90Service::NAME, false);
    BaseModule::registerModule(WifiService::NAME, false);
    BaseModule::registerModule(Webserver::NAME, false);
    BaseModule::registerModule(PicoRtc::NAME, false);
    BaseModule::registerModule(Sx1262::NAMES[0], true);
    BaseModule::registerModule(Sx1262::NAMES[1], true);
    BaseModule::registerModule(RadioTunerTx::NAME, false);
    BaseModule::registerModule(RadioTunerRx::NAME, false);
    BaseModule::registerModule(RxDataFrameQueue::NAME, false);
    BaseModule::registerModule(ADSBDecoder::NAME, false);
    BaseModule::registerModule(Flarm2024::NAME, false);
    BaseModule::registerModule(Ogn1::NAME, false);
    BaseModule::registerModule(ADSLAce::NAME, false);
    BaseModule::registerModule(GDLoverUDP::NAME, false);
    BaseModule::registerModule(GpsDecoder::NAME, false);
    BaseModule::registerModule(UbloxM8N::NAME, true);
    BaseModule::registerModule(L76B::NAME, true);
    BaseModule::registerModule(StaticGPS::NAME, false);
    BaseModule::registerModule(SerialADSB::NAME, true);
    BaseModule::registerModule(Dump1090Client::NAME, false);
    BaseModule::registerModule(AircraftTracker::NAME, false);
    BaseModule::registerModule(DataPort::NAME, false);
    BaseModule::registerModule(AirConnect::NAME, false);
    BaseModule::registerModule(GatasConnect::NAME, false);
    BaseModule::registerModule(GatasConnectUDP::NAME, false);
    BaseModule::registerModule(Bluetooth::NAME, false);
    BaseModule::registerModule(FanetAce::NAME, false);
    BaseModule::registerModule(Idle::NAME, false);

    for (auto a : BaseModule::registeredModules())
    {
        printf("Registered %s\n", a.first.cbegin());
    }
}

GATAS::GlobalPoolConfiguration pool;

namespace
{
    GATAS::OwnshipState ownshipState;
}

void disabled(etl::string_view name, Configuration &config)
{
    // clang-format off
    if (name == Sx1262::NAMES[0]) {
        Sx1262::enterDisabledState(0, config);
    }
    if (name == Sx1262::NAMES[1]) {
        Sx1262::enterDisabledState(1, config);
    }
    // clang-format on
}

BaseModule *loadModule(etl::string_view name, etl::imessage_bus &bus, Configuration &config)
{
    // clang-format off
    if (name == Ogn1::NAME)
    {
        return new Ogn1(bus, config, ownshipState);
    }
    if (name == FanetAce::NAME)
        return new FanetAce(bus, config, ownshipState);
    if (name == ADSLAce::NAME)
        return new ADSLAce(bus, config, ownshipState);
    if (name == Flarm2024::NAME)
        return new Flarm2024(bus, config, ownshipState);
    if (name == AirConnect::NAME)
        return new AirConnect(bus, config);
    if (name == GatasConnect::NAME)
    {
        return new GatasConnect(bus, config, ownshipState);
    }
    if (name == GatasConnectUDP::NAME)
        return new GatasConnectUDP(bus, config);
    if (name == Bluetooth::NAME)
        return new Bluetooth(bus, config);
    if (name == DataPort::NAME)
        return new DataPort(bus, config, ownshipState);
    if (name == AircraftTracker::NAME)
        return new AircraftTracker(bus, config, ownshipState);
    if (name == Dump1090Client::NAME)
        return new Dump1090Client(bus, config);
    if (name == SerialADSB::NAME)
        return new SerialADSB(bus, config);
    if (name == L76B::NAME)
        return new L76B(bus, config);
    if (name == UbloxM8N::NAME)
        return new UbloxM8N(bus, config);
    if (name == StaticGPS::NAME)
        return new StaticGPS(bus, config);
    if (name == GpsDecoder::NAME)
        return new GpsDecoder(bus, config, ownshipState);
    if (name == GDLoverUDP::NAME)
        return new GDLoverUDP(bus, config);
    if (name == ADSBDecoder::NAME)
        return new ADSBDecoder(bus, config, ownshipState);
    if (name == RadioTunerRx::NAME)
        return new RadioTunerRx(bus, config, ownshipState);
    if (name == RadioTunerTx::NAME)
        return new RadioTunerTx(bus, config, ownshipState);
    if (name == RxDataFrameQueue::NAME)
        return new RxDataFrameQueue(bus, config);
    if (name == Sx1262::NAMES[0])
        return new Sx1262(bus, config, 0);
    if (name == Sx1262::NAMES[1])
        return new Sx1262(bus, config, 1);
    if (name == PicoRtc::NAME)
        return new PicoRtc(bus, config);
    if (name == Webserver::NAME)
        return new Webserver(bus, config);
    if (name == WifiService::NAME)
        return new WifiService(bus, config);
    if (name == Gdl90Service::NAME)
    {
        return new Gdl90Service(bus, config, ownshipState);
    }
    if (name == Bmp280::NAME)
    {
        return new Bmp280(bus, config, ownshipState);
    }
    if (name == AceSpi::NAMES[0])
        return new AceSpi(bus, config, 0);
    if (name == AceSpi::NAMES[1])
        return new AceSpi(bus, config, 1);
    if (name == Idle::NAME)
        return new Idle(bus, config);
    // if (name == Config::NAME) return new Config(bus, FlashStore, DEFAULT_GATAS_CONFIG); // Uncomment if needed
    // clang-format on

    return nullptr; // Unknown name
}

constexpr size_t VOL_DATA_SIZE = 4096;
uint8_t __uninitialized_ram(store[VOL_DATA_SIZE]);
static InMemoryStore volatileStore{VOL_DATA_SIZE, store};

// Flash memory map, from low to high addresses:
// RP2040: Application | Binary Store | Permanent Store | Bluetooth (2 sectors)
// RP2350:
//   end - 6 sectors: Bluetooth bank 0
//   end - 5 sectors: Bluetooth bank 1
//   end - 4 sectors: Binary Store
//   end - 3 sectors: Permanent Store (application configuration, unchanged)
//   end - 2 sectors: unused
//   end - 1 sector:  E10 reserved
//
// RP2350's SDK default places Bluetooth bank 0 at the same offset as the
// existing Permanent Store. The project configuration relocates the RP2350
// Bluetooth banks while retaining the established OpenAce store addresses.
// SEE PICO_FLASH_BANK_STORAGE_OFFSET in config.hpp.in

constexpr size_t PERMSTORE_NUM_SECTORS = (VOL_DATA_SIZE + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
constexpr size_t BINSTORE_NUM_SECTORS = (sizeof(GATAS::BinaryStore) + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

// Used to store Application Configuration
static FlashStore permanentStore{PERMSTORE_NUM_SECTORS * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE * 3};
// Used to store runtime information not stored in permanent store, counters, id's etc...
static FlashStore binaryStore{BINSTORE_NUM_SECTORS * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE * 4};

static GATAS::ThreadSafeBus<24> bus;

static Config config(bus, volatileStore, permanentStore, binaryStore, DEFAULT_GATAS_CONFIG);
volatile static bool loadIndicator = false;
volatile static int8_t ledStatusIndicatorPin = -1;

static bool hasHardwareConfiguration(const etl::string_view moduleName, const Configuration &config)
{
    if (moduleName.ends_with(etl::string_view("_0")))
    {
        return !config.pinMap(moduleName, moduleName.substr(0, moduleName.size() - 2)).empty();
    }

    return !config.pinMap(moduleName).empty();
}

static void load(const etl::string_view str, etl::imessage_bus &bus, Configuration &config, bool force = false)
{
    if (ledStatusIndicatorPin > -1)
    {
        gpio_put(ledStatusIndicatorPin, loadIndicator);
        loadIndicator = !loadIndicator;
    }

#if GATAS_DEBUG == 1
    struct HeapLogger
    {
        ~HeapLogger() { printf("\nFree: %d\n\n", xPortGetFreeHeapSize()); }
    } heapLogger;
#endif

    auto registeredModules = BaseModule::registeredModules();

    printf("\nLoading %s ... ", str.cbegin());

    if (registeredModules[str].hwCheck && !hasHardwareConfiguration(str, config))
    {
        BaseModule::setModuleStatus(str, GATAS::PostConstruct::HARDWARE_NOT_CONFIGURED);
        printf("not configured for this device, skipping ");
        return;
    }

    if (!(config.isModuleEnabled(str) || force))
    {
        printf("disabled ");
        disabled(str, config);
        return;
    }

    if (!registeredModules.contains(str))
    {
        printf("-> not Found ");
        return;
    }

    auto *module = loadModule(str, bus, config);

    if (!module)
    {
        printf("-> out of memory ");
        return;
    }

    printf("-> PostConstruct() ");
    auto result = module->postConstruct();
    if (result == GATAS::PostConstruct::OK)
    {
        BaseModule::setModuleStatus(str, module);
        printf("-> start() ");
        module->start();
    }
    else
    {
        BaseModule::setModuleStatus(str, result);
        printf("-> Unloading reason [%s] ", postConstructToString(result));
        // TODO: Find out why deleting a module
        // For some strange reason when deleting a module, I get a hard crash
        // delete module;
    }
}

static void loadModules(void *arg)
{
    (void)arg;

    CoreUtils::init();
    ownshipState.init(CoreUtils::sharedSpinLock());
    config.postConstruct();
    config.start();

    BaseModule::setModuleStatus(Configuration::NAME, &config);

    load(WifiService::NAME, bus, config, true);

    WifiService *client = (WifiService *)(config.moduleByName(config, WifiService::NAME));
    if (client != nullptr)
    {
        load(Webserver::NAME, bus, config, true);
        load(Bluetooth::NAME, bus, config, true);
    }
    load(AircraftTracker::NAME, bus, config, true);
    load(AceSpi::NAMES[0], bus, config, true);
    load(AceSpi::NAMES[1], bus, config, true);

    // Hardware timings, GPS and connectivity
    load(PicoRtc::NAME, bus, config, true);
    load(GpsDecoder::NAME, bus, config);

    // GPS
    load(UbloxM8N::NAME, bus, config);
    load(L76B::NAME, bus, config);
    load(StaticGPS::NAME, bus, config); // Must always be after any Hardware GPS
    // GPS

    load(Gdl90Service::NAME, bus, config);
    load(GDLoverUDP::NAME, bus, config);
    load(DataPort::NAME, bus, config);
    load(AirConnect::NAME, bus, config);
    load(GatasConnect::NAME, bus, config);
    load(GatasConnectUDP::NAME, bus, config);

    load(RxDataFrameQueue::NAME, bus, config, true);
    for (uint8_t i = 0; i < GATAS_MAX_RADIOS; i++)
    {
        load(Sx1262::NAMES[i], bus, config);
    }

    // Loaded after Sx1262 for now due to conflict at startup (SPI bus is not locked at that time)
    load(Bmp280::NAME, bus, config);

    // Other for these two are currently important to ensure configuration on TX is set before RX
    // see RadioTunerRx::enableDisableDatasources()
    // Data sources
    load(RadioTunerTx::NAME, bus, config);
    load(RadioTunerRx::NAME, bus, config);
    load(ADSBDecoder::NAME, bus, config);

    // Protocols
    load(ADSLAce::NAME, bus, config);
    load(FanetAce::NAME, bus, config);
    load(Flarm2024::NAME, bus, config);
    load(Ogn1::NAME, bus, config);
    load(Dump1090Client::NAME, bus, config);

    // Must be loaded last because some modules uses these to send messages to other modules and we need to ensure that all modules are loaded
    load(Idle::NAME, bus, config, true);

    // SerialADSB messes up the serial terminal, but it will load beyond this point
    // load(SerialADSB::NAME, bus, config);
    // puts("\033[2J\033[H");

    // All successfully initialised modules have now completed start() and can
    // safely receive messages emitted by post-start lifecycle hooks.
    for (const auto &registeredModule : BaseModule::registeredModules())
    {
        const auto &moduleStatus = registeredModule.second;
        if (moduleStatus.result == GATAS::PostConstruct::OK && moduleStatus.module != nullptr)
        {
            moduleStatus.module->postStart();
        }
    }

    printf(
        R"=(

     █████████    █████████   ███████████   █████████    █████████
    ███░░░░░███  ███░░░░░███ ░█░░░███░░░█  ███░░░░░███  ███░░░░░███
   ███     ░░░  ░███    ░███ ░   ░███  ░  ░███    ░███ ░███    ░░░
  ░███          ░███████████     ░███     ░███████████ ░░█████████
  ░███    █████ ░███░░░░░███     ░███     ░███░░░░░███  ░░░░░░░░███
  ░░███  ░░███  ░███    ░███     ░███     ░███    ░███  ███    ░███
   ░░█████████  █████   █████    █████    █████   █████░░█████████
    ░░░░░░░░░  ░░░░░   ░░░░░    ░░░░░    ░░░░░   ░░░░░  ░░░░░░░░░

        GATAS Device ID: %lX

        )=",
        static_cast<uint32_t>(config.internalStore()->gatasId));
    gpio_put(ledStatusIndicatorPin, 1);

    vTaskDelete(nullptr);
}

#if configGENERATE_RUN_TIME_STATS == 1 && configSHOW_RUN_TIME_STATS == 1
namespace
{
    struct PreviousTaskRuntime
    {
        TaskHandle_t handle;
        configRUN_TIME_COUNTER_TYPE runTimeCounter;
    };

    const char *taskStateToString(const eTaskState state)
    {
        switch (state)
        {
        case eRunning:
            return "Run";
        case eReady:
            return "Ready";
        case eBlocked:
            return "Block";
        case eSuspended:
            return "Susp";
        case eDeleted:
            return "Del";
        case eInvalid:
        default:
            return "Inv";
        }
    }

    configRUN_TIME_COUNTER_TYPE previousRunTimeForTask(const PreviousTaskRuntime *entries,
                                                       const UBaseType_t numEntries,
                                                       const TaskHandle_t handle)
    {
        for (UBaseType_t i = 0; i < numEntries; i++)
        {
            if (entries[i].handle == handle)
            {
                return entries[i].runTimeCounter;
            }
        }

        return 0;
    }

    bool isFreeRtosIdleTask(const char *taskName)
    {
        return strncmp(taskName, "IDLE", 4) == 0;
    }
} // namespace

void vDiagnosticsTask(void *pvParameters)
{
    (void)pvParameters;
    configRUN_TIME_COUNTER_TYPE previousTotalRunTime = 0;
    PreviousTaskRuntime *previousTaskRuntimes = nullptr;
    UBaseType_t previousNumTasks = 0;

    while (true)
    {
        UBaseType_t numTasks = uxTaskGetNumberOfTasks();
        TaskStatus_t *taskStatusArray = static_cast<TaskStatus_t *>(pvPortMalloc(numTasks * sizeof(TaskStatus_t)));

        if (taskStatusArray)
        {
            configRUN_TIME_COUNTER_TYPE totalRunTime = 0;
            numTasks = uxTaskGetSystemState(taskStatusArray, numTasks, &totalRunTime);

            qsort(taskStatusArray, numTasks, sizeof(TaskStatus_t), [](const void *a, const void *b)
                  { return strcasecmp(((TaskStatus_t *)a)->pcTaskName, ((TaskStatus_t *)b)->pcTaskName); });

            const configRUN_TIME_COUNTER_TYPE deltaTotalRunTime =
                (totalRunTime >= previousTotalRunTime) ? (totalRunTime - previousTotalRunTime) : 0;
            const float windowSeconds = static_cast<float>(deltaTotalRunTime) / 1000000.0f;
            const float windowCapacitySeconds = windowSeconds * static_cast<float>(configNUMBER_OF_CORES);
            configRUN_TIME_COUNTER_TYPE idleWindowRunTime = 0;

            for (UBaseType_t i = 0; i < numTasks; i++)
            {
                const configRUN_TIME_COUNTER_TYPE previousRunTime =
                    previousRunTimeForTask(previousTaskRuntimes, previousNumTasks, taskStatusArray[i].xHandle);
                const configRUN_TIME_COUNTER_TYPE deltaRunTime =
                    (taskStatusArray[i].ulRunTimeCounter >= previousRunTime)
                        ? (taskStatusArray[i].ulRunTimeCounter - previousRunTime)
                        : 0;

                if (isFreeRtosIdleTask(taskStatusArray[i].pcTaskName))
                {
                    idleWindowRunTime += deltaRunTime;
                }
            }

            const float idleSystemPercent =
                (deltaTotalRunTime > 0)
                    ? (100.0f * static_cast<float>(idleWindowRunTime) /
                       (static_cast<float>(deltaTotalRunTime) * static_cast<float>(configNUMBER_OF_CORES)))
                    : 0.0f;
            const float busySystemPercent = 100.0f - idleSystemPercent;

            puts("\033[2J\033[H");
            printf("Task snapshot: %lu tasks | Heap free %lu / %lu bytes | CPU window %.2f s on %u cores (%.2f core-s)\n",
                   static_cast<unsigned long>(numTasks),
                   static_cast<unsigned long>(getFreeHeap()),
                   static_cast<unsigned long>(getTotalHeap()),
                   windowSeconds,
                   static_cast<unsigned int>(configNUMBER_OF_CORES),
                   windowCapacitySeconds);
            printf("System load: Busy %.2f%% | FreeRTOS idle %.2f%%\n", busySystemPercent, idleSystemPercent);
            puts("Note: BootC%/WinC% are relative to one core, so totals can exceed 100% on this dual-core SMP build.");
            puts("      WinSys% is relative to total CPU capacity across both cores. DiagTask can spike when printing.");
            puts("Task Name        Boot us     BootC%  Win us      WinC%   WinSys% State  Pri  Stack Left");
            puts("-------------------------------------------------------------------------------------------");

            for (UBaseType_t i = 0; i < numTasks; i++)
            {
                const configRUN_TIME_COUNTER_TYPE previousRunTime =
                    previousRunTimeForTask(previousTaskRuntimes, previousNumTasks, taskStatusArray[i].xHandle);
                const configRUN_TIME_COUNTER_TYPE deltaRunTime =
                    (taskStatusArray[i].ulRunTimeCounter >= previousRunTime)
                        ? (taskStatusArray[i].ulRunTimeCounter - previousRunTime)
                        : 0;
                const float bootPercent =
                    (totalRunTime > 0)
                        ? (100.0f * static_cast<float>(taskStatusArray[i].ulRunTimeCounter) / static_cast<float>(totalRunTime))
                        : 0.0f;
                const float windowPercent =
                    (deltaTotalRunTime > 0)
                        ? (100.0f * static_cast<float>(deltaRunTime) / static_cast<float>(deltaTotalRunTime))
                        : 0.0f;
                const float windowSystemPercent =
                    (deltaTotalRunTime > 0)
                        ? (100.0f * static_cast<float>(deltaRunTime) /
                           (static_cast<float>(deltaTotalRunTime) * static_cast<float>(configNUMBER_OF_CORES)))
                        : 0.0f;

                printf("%-16s %-11lu %-7.2f %-11lu %-7.2f %-7.2f %-6s %-4lu %-10lu\n",
                       taskStatusArray[i].pcTaskName,
                       static_cast<unsigned long>(taskStatusArray[i].ulRunTimeCounter),
                       bootPercent,
                       static_cast<unsigned long>(deltaRunTime),
                       windowPercent,
                       windowSystemPercent,
                       taskStateToString(taskStatusArray[i].eCurrentState),
                       taskStatusArray[i].uxCurrentPriority,
                       uxTaskGetStackHighWaterMark(taskStatusArray[i].xHandle));
            }

            PreviousTaskRuntime *currentTaskRuntimes =
                static_cast<PreviousTaskRuntime *>(pvPortMalloc(numTasks * sizeof(PreviousTaskRuntime)));

            if (currentTaskRuntimes)
            {
                for (UBaseType_t i = 0; i < numTasks; i++)
                {
                    currentTaskRuntimes[i].handle = taskStatusArray[i].xHandle;
                    currentTaskRuntimes[i].runTimeCounter = taskStatusArray[i].ulRunTimeCounter;
                }

                if (previousTaskRuntimes)
                {
                    vPortFree(previousTaskRuntimes);
                }

                previousTaskRuntimes = currentTaskRuntimes;
                previousNumTasks = numTasks;
                previousTotalRunTime = totalRunTime;
            }

            vPortFree(taskStatusArray);
        }

        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, TASK_DELAY_MS(5'000));
    }
}
#endif

//  WifiService::PostConstruct()...assertion "get_core_num() == async_context_core_num(cyw43_async_context)" failed: file "/opt/pico/pico-sdk/src/rp2_common/pico_cyw43_driver/cyw43_driver.c", line 54, function: cyw43_irq_init

// #if PICO_CYW43_ARCH_DEBUG_ENABLED
// #define CYW43_ARCH_DEBUG(...) printf(__VA_ARGS__)
// #else
// #define CYW43_ARCH_DEBUG(...) ((void)0)
// #endif

void vLaunch(void)
{
    /*** Turn on LED ASAP to indicate that the device is on */
    ledStatusIndicatorPin = config.valueByPath(26, "port5", "O0");
    gpio_init(ledStatusIndicatorPin);
    gpio_set_dir(ledStatusIndicatorPin, GPIO_OUT);
    gpio_put(ledStatusIndicatorPin, 1); // Turn on LED to indicate booting
    /*** Turn on LED ASAP to indicate that the device is on */

    // Bootstrap
    BaseModule::initBase();
    registerModules();
    //    BaseModule::setModuleStatus(Configuration::NAME, &config);
    BaseModule::setModuleStatus(Config::NAME, &config);

    // Load all the modules
    TaskHandle_t task;
    xTaskCreate(loadModules, "LoadModulesTask", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY, &task);
    vTaskCoreAffinitySet(task, 1);

    // Dump some CPU diagnostics to terminal of all running tasks
#if configGENERATE_RUN_TIME_STATS == 1 && configSHOW_RUN_TIME_STATS == 1
    xTaskCreate(vDiagnosticsTask, "DiagTask", configMINIMAL_STACK_SIZE + 64, nullptr, tskIDLE_PRIORITY, nullptr);
#endif

#if NO_SYS && configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    // we must bind the main task to one core (well at least while the init is called)
    // (note we only do this in NO_SYS mode, because cyw43_arch_freertos
    // takes care of it otherwise)
    vTaskCoreAffinitySet(task, 1);
#endif
    /* Start the tasks and timer running. */
    vTaskStartScheduler();
}

void overflowTest()
{
    uint32_t time = 0x000000001;  // Time overflows
    uint32_t start = 0xfffffff0;  // 'old' time
    uint32_t diff = time - start; // Calculate difference
    if (diff != 17)               // Should be 17 if overflow is handled correctly
    {
        panic("Compiler or CPU does not handle overflow correctly");
    }

    // THis must be enabled because we rely on ETL_HAS_STRING_TRUNCATION_CHECKS
    etl::string<12> text = "1234567";
    text += "1234567";
    if (!text.is_truncated())
    {
        panic("String truncate must be enabled");
    }
}

int main()
{
    bool at200Mhz = false;
    if (set_sys_clock_khz(200000, true))
    {
        at200Mhz = true;
    }
    stdio_init_all();
    overflowTest();

#if GATAS_DEBUG == 1
    etl::error_handler::set_callback<etlcpp_receive_error>();
#endif

    const char *rtos_name;
#if (configNUMBER_OF_CORES > 1)
    rtos_name = "GaTas FreeRTOS SMP";
#else
    rtos_name = "GaTas FreeRTOS";
#endif

#if (configNUMBER_OF_CORES > 1)
    printf("        Starting %s on both cores at %dMHZ:\n\n", rtos_name, at200Mhz ? 200 : 125);
    vLaunch();
#elif (RUN_FREERTOS_ON_CORE == 1)
    printf("        Starting %s on core 1:\n\n", rtos_name);
    multicore_launch_core1(vLaunch);
    while (true)
    {
        ;
    }
#else
    printf("        Starting %s on core 0 %dMHZ:\n\n", rtos_name, at200Mhz ? 200 : 125);
    vLaunch();
#endif

    return 0;
}
