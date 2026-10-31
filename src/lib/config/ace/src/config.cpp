
#include "../config.hpp"

#include <cmath>
#include <cstring>

#include "etl/string_utilities.h"

/* GATAS. */
#include "ace/coreutils.hpp"
#include "ace/binarymessages.hpp"
#include "ace/moreutils.hpp"

#include "etl/string.h"
#include "etl/algorithm.h"

#include "pico/rand.h"

static constexpr char SIGNATURE[] = "signature";

etl::string_view Config::loadLocationToString(LoadLocation location) const
{
    switch (location)
    {
    case LoadLocation::VOLATILE:
        return "Ram";
    case LoadLocation::PERSISTENT:
        return "Flash";
    case LoadLocation::DEFAULT:
        return "Default";
    default:
        return "-";
    }
}

GATAS::PostConstruct Config::postConstruct()
{
    pendingDeviceAction.init(CoreUtils::sharedSpinLock());
    auto iStore = internalStore();
    if (iStore->magic != GATAS::BinaryStore::MAGIC)
    {
        GATAS::BinaryStore defaultStore;
        defaultStore.magic = GATAS::BinaryStore::MAGIC;
        defaultStore.gatasId = get_rand_64();
        internalStore(defaultStore);
    }

    // Load a configuration in this order
    auto loadDefaultConfig = false; // Only usefull for developers, this ensures the default config is always loaded when set to true
    if (loadDefaultConfig)
    {
        volatileStore.rewind();
        volatileStore.write(defaultConfig, strlen((const char *)defaultConfig) + 1);
        deserializeJson(doc, volatileStore.data());
        // Don't store here yet, let the user save it for himself first
        //        serializeToPersistent();
        statistics.location = DEFAULT;
    }

    volatileStore.rewind();
    permanentStore.rewind();
    auto error = deserializeJson(doc, volatileStore.data());
    auto signatureMismatch = doc[SIGNATURE].as<uint32_t>() != GATAS_FLASH_SIGNATURE;
    if (error || signatureMismatch)
    {
        error = deserializeJson(doc, permanentStore.data());
        signatureMismatch = doc[SIGNATURE].as<uint32_t>() != GATAS_FLASH_SIGNATURE;

        // Still error, load default
        if (error || signatureMismatch)
        {
            volatileStore.rewind();
            volatileStore.write(defaultConfig, strlen((const char *)defaultConfig) + 1);
            deserializeJson(doc, volatileStore.data());
            statistics.location = DEFAULT;
        }
        else
        {
            statistics.location = PERSISTENT;
            statistics.persistentStoreSize = strlen((const char *)permanentStore.data()) + 1;
            serializeToVolatile();
        }
    }
    else
    {
        statistics.location = VOLATILE;
        // The backing RAM survives a watchdog reboot, but the InMemoryStore
        // write position does not. Re-serialize the validated document so the
        // store's logical size is restored as well.
        serializeToVolatile();
    }

    configMutex = xSemaphoreCreateMutex();

    return GATAS::PostConstruct::OK;
}

void Config::start()
{
    getBus().subscribe(*this);
}

void Config::getData(etl::string_stream &stream, const etl::string_view fullPath) const
{
    auto guard = SemaphoreGuard(5000, configMutex);
    if (!guard)
    {
        return;
    }
    struct CustomWriter
    {
        etl::string_stream &stream;
        CustomWriter(etl::string_stream &stream_) : stream(stream_) {};

        size_t write(uint8_t c)
        {
            stream.str().append(1, (char)c);
            return 1;
        }

        size_t write(const uint8_t *buffer, size_t length)
        {
            etl::string_view view((const char *)buffer, length);
            stream << view;
            return length;
        }
    };

    auto [idx, path] = getConfigPath(fullPath);

    // Test for data from configuration, or from the module itself.
    if (path.size() == 0)
    {
        stream << "{";
        stream << "\"configuration\":\"" << loadLocationToString(statistics.location) << "\"";
        stream << ",\"pStoreSize\":" << statistics.persistentStoreSize;
        stream << ",\"gatasId\":" << static_cast<uint32_t>(internalStore()->gatasId);
        stream << "}";
    }
    else
    {
        auto src = configValueBypath<JsonVariantConst>(path);
        if (idx.has_value())
        {
            auto array = src.as<JsonArrayConst>();
            if (array)
            {
                src = array[idx.value()];
            }
            else
            {
                return;
            }
        }

        CustomWriter writer(stream);
        serializeJson(src, writer);
    }
}

bool Config::setData(const etl::string_view data, const etl::string_view fullPath)
{
    auto guard = SemaphoreGuard(5000, configMutex);
    if (!guard)
    {
        return false;
    }
    auto [idx, path] = getConfigPath(fullPath);
    bool dataMutated = false;
    bool requestSucceeded = false;

    auto applyUpdate = [&](auto &&destination)
    {
        auto error = deserializeJson(destination, data.cbegin(), data.size());

// STACK on my developer device (RP2040 ) is getting a bit low, so disabled this extra check.
#if defined(PICO_RP2350)
        bool capacityOk = measureJson(doc) + 16 <= volatileStore.capacity();
#else
        bool capacityOk = true;
#endif

        if (error == DeserializationError::Ok && capacityOk)
        {
            return true;
        }

        // deserializeJson() clears its destination before parsing and can leave
        // a partial value on failure. The volatile store contains the last
        // complete configuration, so restore it to keep failed updates atomic.
        if (deserializeJson(doc, volatileStore.data()) != DeserializationError::Ok)
        {
            GATAS_WARN("Failed to restore configuration after invalid JSON");
        }
        return false;
    };

    if (idx.has_value())
    {
        auto src = configValueBypath<JsonVariant>(path);
        auto array = src.as<JsonArray>();
        if (array && idx.value() >= 0 && static_cast<size_t>(idx.value()) < array.size())
        {
            if (applyUpdate(array[idx.value()]))
            {
                dataMutated = true;
                requestSucceeded = true;
            }
        }
        else
        {
            GATAS_WARN("Failed to mutate data");
        }
    }
    else
    {
        if (path.size() > 0)
        {
            if (path.back() == "SaveBr")
            {
                requestSucceeded = saveToFlash();
            }
#if GATAS_DEBUG == 1
            // Added for automated testing. This endpoint is excluded from release firmware.
            else if (path.back() == "EraseBr")
            {
                auto persistentBytesErased = permanentStore.erase();
                volatileStore.erase();
                if (persistentBytesErased > 0)
                {
                    statistics.persistentStoreSize = 0;
                }
                requestSucceeded = true;
            }
            else if (path.back() == "CompareBr")
            {
                requestSucceeded = persistentMatchesVolatile();
                if (requestSucceeded)
                {
                    GATAS_INFO("persistent Matches Volatile");
                }
                else
                {
                    GATAS_WARN("persistent does not Match Volatile");
                }
            }
#endif
            // TODO: See if its possible to make something that these two are not in the config
            else if (path.back() == "Restart")
            {
                getBus().receive(GATAS::SystemActionMsg{GATAS::DeviceAction::Restart});
                requestSucceeded = true;
            }
            // Restart the Pico in USB mode so that the user can upload a new firmware
            else if (path.back() == "UsbBoot")
            {
                getBus().receive(GATAS::SystemActionMsg{GATAS::DeviceAction::UsbBoot});
                requestSucceeded = true;
            }
            else
            {
                auto destination = configValueBypath<JsonVariant>(path);
                bool updateApplied = false;
                if (destination != nullptr)
                {
                    updateApplied = applyUpdate(destination);
                }
                else
                {
                    const auto key = path.back();
                    path.pop_back();

                    if (path.size() == 0)
                    {
                        updateApplied = applyUpdate(doc[const_cast<char *>(key.c_str())]);
                    }
                    else
                    {
                        auto parent = configValueBypath<JsonVariant>(path);
                        if (parent != nullptr)
                        {
                            updateApplied = applyUpdate(parent[const_cast<char *>(key.c_str())]);
                        }
                    }
                }

                if (updateApplied)
                {
                    dataMutated = true;
                    requestSucceeded = true;
                }
            }
        }
    }

    if (dataMutated)
    {
        doc["config"]["_dirty"] = true;
        serializeToVolatile();

        // Special-case: If aircraft or config entry was modified, inform all modules that would listen to Configuration::NAME
        if (path[0] == "aircraft" || path[0] == "config")
        {
            getBus().receive(GATAS::ConfigUpdatedMsg{*this, Configuration::NAME});
        }
        else
        {
            // Inform the module itself
            getBus().receive(GATAS::ConfigUpdatedMsg{*this, path[0]});
        }
    }

    return requestSucceeded;
}

bool Config::saveToFlash()
{
    doc["config"]["_dirty"] = false;
    // Check both capacities before changing the retained RAM image.
    const size_t required = measureJson(doc) + 1;
    if (required > volatileStore.capacity() || required > permanentStore.capacity())
    {
        doc["config"]["_dirty"] = true;
        return false;
    }
    serializeToVolatile();
    const bool saved = serializeToPersistent();
    if (!saved)
    {
        doc["config"]["_dirty"] = true;
        serializeToVolatile();
    }
    getBus().receive(GATAS::ConfigUpdatedMsg{*this, Configuration::NAME});
    return saved;
}

void Config::serializeToVolatile()
{
    volatileStore.rewind();
    serializeJson(doc, volatileStore);
    volatileStore.write(0);
}

bool Config::serializeToPersistent()
{
    // One byte operations not yet supported, and arduinoJson wants to do these
    // It might not work at all due to complexity of flash (no interrupts etc)
    // So for now we do this from volatileStore

    if (persistentMatchesVolatile())
    {
        return true;
    }
    permanentStore.rewind();
    const auto written = permanentStore.write(volatileStore.data(), volatileStore.writtenSize());
    if (written != volatileStore.writtenSize() || !persistentMatchesVolatile())
    {
        return false;
    }
    statistics.persistentStoreSize = volatileStore.writtenSize();
    return true;
}

bool Config::persistentMatchesVolatile() const
{
    const auto volatileSize = volatileStore.writtenSize();
    return volatileSize > 0 &&
           volatileSize <= permanentStore.capacity() &&
           std::memcmp(volatileStore.data(), permanentStore.data(), volatileSize) == 0;
}

bool Config::deleteData(const etl::string_view fullPath)
{
    auto guard = SemaphoreGuard(5000, configMutex);
    if (!guard)
    {
        return false;
    }
    bool dataMutated = false;
    auto [idx, path] = getConfigPath(fullPath);

    JsonVariant src = configValueBypath<JsonVariant>(path);
    if (idx.has_value())
    {
        auto array = src.as<JsonArray>();
        if (array)
        {
            src.remove(idx.value());
            dataMutated = true;
        }
    }
    else
    {
        dataMutated = true;
        auto key = path.back();
        path.pop_back();
        src = configValueBypath<JsonVariant>(path);
        src.remove(key.c_str());
    }

    if (dataMutated)
    {
        doc["config"]["_dirty"] = true;
        serializeToVolatile();
    }
    return dataMutated;
}

void Config::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

const GATAS::PinTypeMap Config::pinMap(const etl::string_view moduleName, const etl::string_view fallback) const
{
    GATAS::PinTypeMap map;
    ccharptr hardware = (ccharptr)doc["hardware"]["type"];

    auto loadPinMap = [&](const etl::string_view name) -> bool
    {
        JsonObjectConst moduleConfig = doc[hardware][name];
        if (moduleConfig.isNull())
        {
            return false;
        }

        for (JsonPairConst kv : moduleConfig)
        {
            auto pinType = GATAS::stringToPinType(kv.key().c_str());
            if (pinType != GATAS::PinType::UNKNOWN)
            {
                map[pinType] = kv.value().as<uint8_t>();
            }
        }
        return true;
    };

    if (!loadPinMap(moduleName) && !fallback.empty())
    {
        loadPinMap(fallback);
    }

    return map;
}

const GATAS::Config::WifiServiceData Config::wifiService() const
{
    auto wifi = doc["WifiService"];
    GATAS::Config::WifiServiceData wifiService;

    GATAS::SsidOrPasswdStr ssid = (ccharptr)wifi["ap"]["ssid"];
    GATAS::SsidOrPasswdStr password = (ccharptr)wifi["ap"]["password"];

    if (ssid.size() < 3 || password.size() < 8)
    {
        // Set default Ssid and passsword if non was found
        wifiService.ap.ssid = "GATAS";
        wifiService.ap.password = "12345678";
    }
    else
    {
        wifiService.ap.ssid = ssid;
        wifiService.ap.password = password;
    }

    // Temporary disabled, this might confuse people
    wifiService.apDisabled = false; // (bool)wifi["ap"]["disabled"];

    // Set clients usernames and passwords
    for (auto client : wifi["clients"].as<JsonArrayConst>())
    {
        if (!wifiService.clients.full())
        {
            wifiService.clients.emplace_back((ccharptr)client["ssid"], (ccharptr)client["password"]);
        }
    }

    return wifiService;
};

const GATAS::Config::GaTasConfiguration Config::gaTasConfig() const
{
    ccharptr aircraftId = (ccharptr)doc["config"]["aircraftId"];
    JsonObjectConst aircraftConfig = doc["aircraft"][aircraftId];

    // Default if no aircraft config was found
    if (aircraftConfig.isNull())
    {
        return {
            .conspicuity = {
                .icaoAddress = 0,
                .category = GATAS::AircraftCategory::LIGHT,
                .addressType = GATAS::AddressType::ADSL,
                .stealth = false,
                .noTrack = false,
                .groundStation = false,
                .heightAboveGps = 0},
            .protocols = {},
            .allIcaoAddresses = {}};
    }

    etl::vector<GATAS::DataSourceConfig, static_cast<uint8_t>(GATAS::DataSource::_TRANSPROTOCOLS)> protocols;
    auto appendProtocol = [&](GATAS::DataSource dataSource, GATAS::DataSourceMode mode)
    {
        if (dataSource == GATAS::DataSource::NONE || protocols.full())
        {
            return;
        }

        protocols.push_back({dataSource, mode});

        // Legacy configs expect ADSL to enable the header protocol as well.
        if (dataSource == GATAS::DataSource::ADSLM && !protocols.full())
        {
            protocols.push_back({GATAS::DataSource::ADSLO_HDR, mode});
        }
    };

    for (JsonVariantConst protocol : aircraftConfig["protocols"].as<JsonArrayConst>())
    {
        if (protocol.is<const char *>())
        {
            // Backwards compatibility: old format is an array of protocol names.
            appendProtocol(GATAS::stringToDataSource(protocol.as<const char *>()), GATAS::DataSourceMode::RX_TX);
            continue;
        }

        if (!protocol.is<JsonObjectConst>())
        {
            continue;
        }

        for (JsonPairConst protocolEntry : protocol.as<JsonObjectConst>())
        {
            auto dataSource = GATAS::stringToDataSource(protocolEntry.key().c_str());
            if (dataSource == GATAS::DataSource::NONE)
            {
                continue;
            }

            GATAS::DataSourceMode mode = GATAS::DataSourceMode::RX_TX;
            JsonObjectConst protocolConfig = protocolEntry.value().as<JsonObjectConst>();
            if (!protocolConfig.isNull())
            {
                auto modeStr = protocolConfig["mode"].as<const char *>();
                if (modeStr != nullptr)
                {
                    mode = GATAS::stringToDataSourceMode(modeStr);
                }
            }

            appendProtocol(dataSource, mode);
        }
    }

    etl::vector<uint32_t, GATAS::MAX_AIRCRAFT_CONFIG> allIcaoAddresses;
    auto aircrafts = doc["aircraft"];
    for (JsonPairConst kv : aircrafts.as<JsonObjectConst>())
    {
        if (!allIcaoAddresses.full())
        {
            uint32_t address = kv.value()["address"];
            allIcaoAddresses.push_back(address);
        }
    }

    bool groundStation = aircraftConfig["groundStation"];
    int16_t hag = etl::clamp(static_cast<int16_t>(aircraftConfig["heightAboveGps"]), static_cast<int16_t>(0), static_cast<int16_t>(1500));
    return {
        .conspicuity = {
            .icaoAddress = aircraftConfig["address"],
            .category = BinaryMessages::mapAircraftCategoryToType((ccharptr)aircraftConfig["category"]),
            .addressType = GATAS::stringToAddressType((ccharptr)aircraftConfig["addressType"]),
            .stealth = aircraftConfig["stealth"],
            .noTrack = aircraftConfig["noTrack"],
            .groundStation = groundStation,
            .heightAboveGps = groundStation ? hag : static_cast<int16_t>(0),
        },
        .protocols = protocols,
        .allIcaoAddresses = allIcaoAddresses};
};

bool Config::isModuleEnabled(const etl::string_view moduleName) const
{
    using Token = etl::optional<etl::string_view>;
    Token token;
    etl::string_view view = doc["modules"].as<const char *>();
    while ((token = etl::get_token(view, ",", token, true)))
    {
        if (token.value() == moduleName)
        {
            return true;
        }
    }
    return false;
};

bool Config::isDirty() const
{
    return valueByPath(0, "config", "_dirty") != 0;
}

int Config::valueByPath(int defaultValue, const etl::string_view pathToValue, const etl::string_view key) const
{
    auto path = CoreUtils::parsePath(pathToValue, key);
    auto src = configValueBypath<JsonVariantConst>(path);

    if (src.isNull())
    {
        return defaultValue;
    }
    else
    {
        return src.as<int>();
    }
}

float Config::floatValueByPath(float defaultValue, const etl::string_view pathToValue, const etl::string_view key) const
{
    const auto path = CoreUtils::parsePath(pathToValue, key);
    const auto src = configValueBypath<JsonVariantConst>(path);

    if (src.isNull())
    {
        return defaultValue;
    }

    if (src.is<float>())
    {
        const float value = src.as<float>();
        return std::isfinite(value) ? value : defaultValue;
    }

    const char *text = src.as<const char *>();
    if (text != nullptr)
    {
        const auto value = etl::to_arithmetic<float>(etl::string_view(text));
        return value && std::isfinite(value.value()) ? value.value() : defaultValue;
    }

    return defaultValue;
}

const GATAS::ConfigString Config::strValueByPath(const etl::string_view defaultValue, const etl::string_view pathToValue, const etl::string_view key) const
{
    auto path = CoreUtils::parsePath(pathToValue, key);
    auto src = configValueBypath<JsonVariantConst>(path);

    if (src.isNull())
    {
        return GATAS::ConfigString(defaultValue);
    }
    else
    {
        auto string = src.as<const char *>();
        return GATAS::ConfigString(string);
    }
};

const GATAS::Config::IpPort Config::ipPortBypath(const etl::string_view pathToValue, const etl::string_view key) const
{
    auto path = CoreUtils::parsePath(pathToValue, key);
    auto src = configValueBypath<JsonVariantConst>(path);

    if (src.isNull())
    {
        return GATAS::Config::IpPort{0xffffffffUL, 0};
    }
    else
    {
        auto ipStr = src["ip"].as<const char *>();
        auto ip = parseIpv4String(ipStr);
        auto port = src["port"].as<uint16_t>();
        return GATAS::Config::IpPort{
            ip,
            port};
    }
}

const GATAS::BinaryStore *Config::internalStore() const
{
    return reinterpret_cast<const GATAS::BinaryStore *>(binaryStore.data());
}

void Config::internalStore(const GATAS::BinaryStore &store)
{
    binaryStore.write(reinterpret_cast<const uint8_t *>(&store), sizeof(GATAS::BinaryStore));
}

GATAS::CallSign Config::getCallSignFromHex(uint32_t transponderHex) const
{
    auto aircrafts = doc["aircraft"];
    for (JsonPairConst kv : aircrafts.as<JsonObjectConst>())
    {
        uint32_t address = kv.value()["address"];
        if (transponderHex == address)
        {
            return kv.value()["callSign"].as<const char *>();
        }
    }

    return "";
}

void Config::setValueBypath(const etl::string_view pathToValue, etl::string_view value)
{
    auto path = CoreUtils::parsePath(pathToValue, "");
    auto src = configValueBypath<JsonVariant>(path);
    src.set(value);
    doc["config"]["_dirty"] = true;
}

void Config::setValueBypath(const etl::string_view pathToValue, uint64_t value)
{
    auto path = CoreUtils::parsePath(pathToValue, "");
    auto src = configValueBypath<JsonVariant>(path);
    src.set(value);
    doc["config"]["_dirty"] = true;
}

void Config::on_receive(const GATAS::SelectAircraftRequestMsg &msg)
{
    if (auto guard = SemaphoreGuard(5000, configMutex))
    {
        const auto callSign = getCallSignFromHex(msg.icaoAddress);
        if (!callSign.empty() && gaTasConfig().conspicuity.icaoAddress != msg.icaoAddress)
        {
            setValueBypath("config/aircraftId", callSign);
            serializeToVolatile();
            getBus().receive(GATAS::ConfigUpdatedMsg{*this, Configuration::NAME});
        }
    }
}

void Config::on_receive(const GATAS::DeviceActionRequestMsg &msg)
{
    pendingDeviceAction.store(etl::optional<GATAS::DeviceAction>{msg.action});
}

void Config::on_receive(const GATAS::IdleMsg &msg)
{
    (void)msg;
    auto action = pendingDeviceAction.load();
    if (!action)
    {
        return;
    }
    switch (action.value())
    {
    case GATAS::DeviceAction::Save:
        if (auto guard = SemaphoreGuard(0, configMutex))
        {
            // We have to saveBR from idle loop, because from a BLE task it kept crashing
            saveToFlash();
        }
        else
        {
            return;
        }
        break;
    case GATAS::DeviceAction::Restart:
    case GATAS::DeviceAction::UsbBoot:
        getBus().receive(GATAS::SystemActionMsg{action.value()});
        break;
    default:
        break;
    }

    pendingDeviceAction.update([action](auto &pending) {
        if (pending == action)
        {
            pending.reset();
        }
    });
}
