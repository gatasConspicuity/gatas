#include <catch2/catch_test_macros.hpp>
#include "ace/basemodule.hpp"

namespace
{
    class StartupModule : public BaseModule
    {
    public:
        static constexpr const char *NAME = "StartupTest";
        bool fail = false;
        bool advertisedDuringStart = false;

        explicit StartupModule(etl::imessage_bus &bus) : BaseModule(bus, NAME) {}

        GATAS::PostConstruct postConstruct() override { return GATAS::PostConstruct::OK; }
        void start() override
        {
            advertisedDuringStart = registeredModules().at(NAME).result == GATAS::PostConstruct::OK;
            if (fail)
            {
                setModuleStatus(NAME, GATAS::PostConstruct::TASK_ERROR);
            }
        }
        void getData(etl::string_stream &, const etl::string_view) const override {}
    };
}

TEST_CASE("Module registration uses the result reported during start", "[startup]")
{
    etl::message_bus<4> bus;
    BaseModule::registerModule(StartupModule::NAME, false);
    StartupModule module(bus);
    SECTION("successful start registers the module afterwards")
    {
        REQUIRE(module.startAndRegister(StartupModule::NAME) == GATAS::PostConstruct::OK);
        REQUIRE_FALSE(module.advertisedDuringStart);
        REQUIRE(BaseModule::registeredModules().at(StartupModule::NAME).module == &module);
    }
    SECTION("failure survives an earlier registry snapshot")
    {
        const auto snapshot = BaseModule::registeredModules();
        module.fail = true;
        REQUIRE(module.startAndRegister(StartupModule::NAME) == GATAS::PostConstruct::TASK_ERROR);
        REQUIRE(snapshot.at(StartupModule::NAME).result == GATAS::PostConstruct::NA);
        const auto &status = BaseModule::registeredModules().at(StartupModule::NAME);
        REQUIRE(status.result == GATAS::PostConstruct::TASK_ERROR);
        REQUIRE(status.module == nullptr);
        REQUIRE_FALSE(module.advertisedDuringStart);
    }
}
