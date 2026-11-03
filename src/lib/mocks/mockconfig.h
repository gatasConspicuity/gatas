#pragma once

#include "ace/basemodule.hpp"

class MockConfig : public Configuration
{

public:
    uint32_t ownIcao=0xACEACE;
    MockConfig(etl::imessage_bus &bus) : Configuration(bus)
    {
    }
    virtual ~MockConfig() = default;

    virtual void start() override
    {
    }
    virtual GATAS::PostConstruct postConstruct() override
    {
        return GATAS::PostConstruct::OK;
    }

    virtual const GATAS::Config::GaTasConfiguration gaTasConfig() const override
    {
        return GATAS::Config::GaTasConfiguration{
            {ownIcao,
             GATAS::AircraftCategory::LIGHT,
             GATAS::AddressType::ICAO,
             false,
             false},
            {{GATAS::DataSource::OGN, GATAS::DataSourceMode::RX_TX}}};
    }

    virtual const GATAS::PinTypeMap pinMap(const etl::string_view moduleName, const etl::string_view fallback = etl::string_view()) const override
    {
        GATAS::PinTypeMap m;
        return m;
    };

    virtual bool deleteData(const etl::string_view fullPath) override
    {
        return true;
    }

    virtual int valueByPath(int defaultValue, const etl::string_view pathToValue, const etl::string_view key) const override
    {
        return 0;
    };

    virtual float floatValueByPath(float defaultValue, const etl::string_view pathToValue, const etl::string_view key) const override
    {
        return defaultValue;
    }

    virtual const GATAS::ConfigString strValueByPath(const etl::string_view defaultValue, const etl::string_view pathToValue, const etl::string_view key) const override
    {
        return "";
    }

    virtual bool isModuleEnabled(const etl::string_view moduleName) const override
    {
        return true;
    }

    virtual bool isDirty() const override
    {
        return false;
    }

    // virtual const GATAS::Config::WifiNamePassword wifiClient() const override
    // {
    //     return GATAS::Config::WifiNamePassword
    //     {
    //         "",
    //         ""
    //     };
    // }

    virtual const GATAS::Config::WifiServiceData wifiService() const override
    {
        return GATAS::Config::WifiServiceData{};
    }

    virtual const GATAS::Config::IpPort ipPortBypath(const etl::string_view pathToValue, const etl::string_view key) const override
    {
        return {0, 0};
    };

    virtual const GATAS::BinaryStore *internalStore() const override
    {
        static GATAS::BinaryStore store{GATAS::BinaryStore::MAGIC, 0xCAFEBABE};

        return &store;
    }

    virtual void internalStore(const GATAS::BinaryStore &store) override
    {
    }

    virtual GATAS::CallSign getCallSignFromHex(uint32_t code) const override
    {
        switch (code) {
            case 0xFF0001: return "PH-ABC";
            case 0xFF0002: return "";
            case 0xFF0003: return "1234 567";
            case 0xFF0004: return "abcdefghijkl";
            case 0xACEACE: return "PH-ACE";
        }
        return "PH-XXX";
    }

};
