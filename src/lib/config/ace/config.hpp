#pragma once

#include <stdio.h>

#include "configstore.hpp"

#include "ace/coreutils.hpp"
#include "ace/spinlockguard.hpp"
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/semaphoreguard.hpp"

#include "etl/string.h"
#include "etl/to_arithmetic.h"
#include "etl/optional.h"

#include "ArduinoJson.hpp"
#include "jsonallocator.hpp"

using namespace ArduinoJson;

typedef std::function<void(const char *NAME, const GATAS::PinTypeMap &map)> LoadModuleCallback;

class Config : public Configuration, public etl::message_router<Config, GATAS::SelectAircraftRequestMsg, GATAS::DeviceActionRequestMsg, GATAS::IdleMsg>
{
private:
    enum LoadLocation : uint8_t
    {
        // Message to indicate next period needs to be decided, send from a timer
        NA = 0,
        VOLATILE = 1,
        PERSISTENT = 2,
        DEFAULT = 3
    };

    struct
    {
        LoadLocation location = NA;
        uint16_t persistentStoreSize = 0;
    } statistics;

    etl::string_view loadLocationToString(LoadLocation location) const;

    using ccharptr = const char *;

    template <typename T>
    T configValueBypathImpl(Config &config, const etl::ivector<GATAS::Modulename> &pathParsed) const
    {
        T variant;
        if (pathParsed.size() == 0)
        {
            return variant;
        }

        variant = config.doc[pathParsed[0]];
        for (uint8_t depth = 1; depth < pathParsed.size(); depth++)
        {
            auto const &key = pathParsed[depth];
            // TODO: Check if we can do without the PATH index check
            auto index = etl::to_arithmetic<size_t>(key);
            if (index.has_value())
            {
                variant = variant[index.value()];
            }
            else
            {
                variant = variant[key];
            }
        }

        return variant;
    }

    template <typename T>
    T configValueBypath(const etl::ivector<GATAS::Modulename> &pathParsed)
    {
        return configValueBypathImpl<T>(*this, pathParsed);
    }

    template <typename T>
    const T configValueBypath(const etl::ivector<GATAS::Modulename> &pathParsed) const
    {
        return configValueBypathImpl<T>(const_cast<Config &>(*this), pathParsed);
    }

    /**
     * Get a configuration value from the configuration
     * The second and third parameter can be an integer to access an array type
     *
     * Example: auto value = configValue("api/_configuration/GDLoverUDP/ips/1.json");
     * Example: auto value = configValue("api/_configuration/GDLoverUDP/ips.json");
     * Example: auto value = configValue("api/_configuration/GDLoverUDP.json");
     */
    // template <typename T>
    // T configValueBypath(const etl::string_view path) const
    // {
    //     const etl::vector<GATAS::Modulename, 7> vpath = CoreUtils::parsePath(path);
    //     return configValueBypath<T>(vpath);
    // }

    /**
     * Prepare the path into the configuration and return a possible index that is the last item of the path
     * Example  : api/modulefoo/bar/bas/1.json  => idx 1  bar/bas
     * Example  : api/modulefoo/bar/bas/beer.json  => idx <no value>  bar/bas/beer
     * When fullPath = true, the json extension and /apt/_Configuration is not part of the path and points directly into configuration
     * returns: Result object
     */
    auto getConfigPath(const etl::string_view path, bool fullPath = true) const
    {
        struct Result
        {
            etl::to_arithmetic_result<int> idx;
            etl::vector<GATAS::Modulename, 7> path;
        };

        auto pathParsed = CoreUtils::parsePath(path, "");
        if (fullPath)
        {
            pathParsed.erase(pathParsed.begin(), pathParsed.begin() + 2); // Removes /api/_Configuration path
            pathParsed.pop_back();                                        // Removes the 'json' extension
        }

        if (pathParsed.size() == 0)
        {
            return Result{
                etl::to_arithmetic_result<int>{},
                pathParsed};
        }

        // When the last last item is a number, it's expected to be an entry in an array
        // return the index and pop it from the path
        auto index = etl::to_arithmetic<int>(pathParsed.back());
        if (index.has_value())
        {
            pathParsed.pop_back();
        }

        return Result{
            index,
            pathParsed};
    }

    void serializeToVolatile();
    bool serializeToPersistent();
    bool persistentMatchesVolatile() const;

private:
    friend class message_router;
    FreeRTOSJsonAllocator jsonAllocator;
    JsonDocument doc{&jsonAllocator};
    ConfigStore &volatileStore;
    ConfigStore &permanentStore;
    ConfigStore &binaryStore;
    const uint8_t *defaultConfig;
    SemaphoreHandle_t configMutex;
    SynchronizedValue<etl::optional<GATAS::DeviceAction>> pendingDeviceAction;
    // Private mutation helpers: the caller must hold configMutex.
    bool saveToFlash();
    void setValueBypath(const etl::string_view pathToValue, etl::string_view value);
    void setValueBypath(const etl::string_view pathToValue, uint64_t value);

public:
    Config(etl::imessage_bus &bus, ConfigStore &volatileStore_, ConfigStore &permanentStore_, ConfigStore &binaryStore_, const uint8_t *defaultConfig_) : Configuration(bus), volatileStore(volatileStore_), permanentStore(permanentStore_), binaryStore(binaryStore_), defaultConfig(defaultConfig_)
    {
    }

    virtual ~Config() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    /**
     * Get data from the modules itself, or from teh configuration
     */
    virtual void getData(etl::string_stream &stream, const etl::string_view fullPath) const override;

    /**
     * Set's data the configuration, can only be called from the Web API and only suppose JSON strings
     */
    virtual bool setData(const etl::string_view data, const etl::string_view fullPath) override;

    virtual bool deleteData(const etl::string_view fullPath) override;

    void on_receive_unknown(const etl::imessage &msg);
    void on_receive(const GATAS::SelectAircraftRequestMsg &msg);
    void on_receive(const GATAS::DeviceActionRequestMsg &msg);
    void on_receive(const GATAS::IdleMsg &msg);

    /**
     * Returns the hardware configuration for this station or Aircraft
     *
     */
    virtual const GATAS::Config::GaTasConfiguration gaTasConfig() const override;

    virtual const GATAS::Config::WifiServiceData wifiService() const override;

    /**
     * Retreives the pin mapping for a given module, usually used for hardware configurations
     */
    virtual const GATAS::PinTypeMap pinMap(const etl::string_view moduleName, const etl::string_view fallback = etl::string_view()) const override;

    virtual int valueByPath(int defaultValue, const etl::string_view pathToValue, const etl::string_view key) const override;

    virtual float floatValueByPath(float defaultValue, const etl::string_view pathToValue, const etl::string_view key) const override;

    virtual const GATAS::ConfigString strValueByPath(const etl::string_view defaultValue, const etl::string_view pathToValue, const etl::string_view key) const override;

    virtual const GATAS::Config::IpPort ipPortBypath(const etl::string_view pathToValue, const etl::string_view key) const override;

    virtual bool isModuleEnabled(const etl::string_view moduleName) const override;

    virtual bool isDirty() const override;

    virtual const GATAS::BinaryStore *internalStore() const override;
    virtual void internalStore(const GATAS::BinaryStore &store) override;

    /**
     * Based on the HEX code, find the CallSign from any of the configured aircraft
     */
    virtual GATAS::CallSign getCallSignFromHex(uint32_t) const override;

};
