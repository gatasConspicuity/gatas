#include <catch2/catch_test_macros.hpp>
#include "ace/lr2021.hpp"
#include "../lr2021driver/inc/lr20xx_hal.h"
#include "mockconfig.h"
#include "etl/message_bus.h"

namespace
{
struct RadioModuleRegistration
{
    explicit RadioModuleRegistration(etl::string_view name) : name(name)
    {
        BaseModule::registerModule(name, true);
    }

    ~RadioModuleRegistration()
    {
        BaseModule::registerModule(name, true);
    }

    const etl::string_view name;
};

GATAS::PinTypeMap pins()
{
    return {{GATAS::PinType::CS, 1}, {GATAS::PinType::BUSY, 2}, {GATAS::PinType::DIO1, 3}};
}
class Config : public MockConfig
{
public:
    using MockConfig::MockConfig;
    const GATAS::PinTypeMap pinMap(etl::string_view, etl::string_view = {}) const override { return pins(); }
    GATAS::ConfigString capabilities = "Gfsk868,Lora868,Gfsk915";
    int valueByPath(int fallback, etl::string_view name, etl::string_view key) const override
    {
        return name == "Lr2021_1" && key == "txEnabled" ? 1 : fallback;
    }
    const GATAS::ConfigString strValueByPath(etl::string_view fallback, etl::string_view name,
                                            etl::string_view key) const override
    {
        if (name == "Lr2021_1" && key == "capabilities")
        {
            return capabilities;
        }
        return GATAS::ConfigString(fallback);
    }
};
class Spi : public SpiModule
{
    SemaphoreHandle_t mutex = reinterpret_cast<void *>(1);
public:
    explicit Spi(etl::imessage_bus &bus) : SpiModule(bus, SpiModule::NAMES[0]) {}
    GATAS::PostConstruct postConstruct() override { return GATAS::PostConstruct::OK; }
    void start() override {}
    void read_registers(uint8_t, uint8_t, uint8_t *, uint16_t, uint8_t) const override {}
    void read_registers_select(uint8_t, uint8_t) const override {}
    void read_registers_read(uint8_t, uint8_t *, uint16_t) const override {}
    void cs_select(uint8_t) const override { RadioTest::transfers.push_back('S'); }
    void cs_deselect(uint8_t) const override { RadioTest::transfers.push_back('D'); }
    void write_array(uint8_t, uint8_t *, uint8_t, uint8_t) const override {}
    void write_byte(uint8_t, uint8_t, uint8_t) const override {}
    SpiGuard getLock(bool &locked) override { return SpiGuard(mutex, locked); }
    uint8_t spiNum() const override { return 0; }
};
class TestLr2021 : public Lr2021
{
public:
    using Lr2021::Lr2021;
    using Lr2021::decodeOokFrame;
    void attach(SpiModule &spi) { spiHall = &spi; }
    void configure(const GATAS::RadioParameters &parameters, bool tx, uint8_t length)
    {
        clearHalError();
        configureRadio(parameters, tx ? RadioDirection::TX : RadioDirection::RX, length);
    }
};
const GATAS::LinkLayerConfig link{1, GATAS::DataSource::OGN, true, 26, 16, 32, 8, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0}};
GATAS::RadioParameters parameters{&link, &GATAS::RadioParameters::DEFAULT, 868200000, 0};
struct Fixture
{
    etl::message_bus<8> bus;
    Config config{bus};
    Spi spi{bus};
    TestLr2021 radio{bus, config, 0, 0};
    Fixture() { RadioTest::reset(); radio.attach(spi); xSemaphoreTakeValue = pdTRUE; }
};
const std::vector<uint8_t> &command(uint16_t opcode)
{
    for (const auto &bytes : RadioTest::writes)
    {
        if (bytes.size() >= 2 && bytes[0] == (opcode >> 8) && bytes[1] == (opcode & 255)) { return bytes; }
    }
    throw std::runtime_error("missing opcode");
}
}

TEST_CASE("LR2021 reads use separate command and response CS transactions")
{
    Fixture f;
    const uint8_t cmd[]{0x01, 0x01};
    uint8_t data[2]{};
    RadioTest::response = {0xff, 0x12, 0x34};
    REQUIRE(lr20xx_hal_read(&f.radio, cmd, 2, data, 2) == LR20XX_HAL_STATUS_OK);
    REQUIRE(RadioTest::transfers == std::vector<char>{'S','W','D','S','R','R','D'});
    REQUIRE(RadioTest::reads == std::vector<size_t>{1, 2});
    REQUIRE(data[0] == 0x12);
    REQUIRE(data[1] == 0x34);
}

TEST_CASE("LR2021 configures the 1090 MHz OOK detector for receive only")
{
    Fixture f;
    const GATAS::RfConfig rf{GATAS::Modulation::OOK, 1'090'000'000, 0, 0, 3'076'000, 2'000'000, 0, 0, GATAS::RadioCapability::Ook1090};
    const GATAS::LinkLayerConfig adsb{10, GATAS::DataSource::ADSB, false, 14, 0, 0, 0, {}};
    const GATAS::RadioParameters profile{&adsb, &rf, rf.baseFrequency, 0};
    f.radio.configure(profile, false, 0);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0281) == std::vector<uint8_t>{2, 0x81, 0, 0x1e, 0x84, 0x80, 0, 0, 0});
    REQUIRE(command(0x0282) == std::vector<uint8_t>{2, 0x82, 0, 16, 0, 0, 27, 0});
    REQUIRE(command(0x0284) == std::vector<uint8_t>{2, 0x84, 0, 0, 0, 0x95, 0x88});
    REQUIRE(command(0x0288) == std::vector<uint8_t>{2, 0x88, 0x02, 0x85, 15, 0, 0});
    RadioTest::writes.clear();
    f.radio.configure(profile, false, 0);
    REQUIRE(RadioTest::writes.empty());
    f.radio.configure(profile, true, 14);
    REQUIRE(f.radio.hasHalError());
    REQUIRE(RadioTest::writes.empty());
    f.radio.configure(parameters, false, 0);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0105) == std::vector<uint8_t>{1, 5, 0xf3, 1, 0x50, 0, 0, 0, 0xff, 0, 0, 0, 0});
}

TEST_CASE("LR2021 reconstructs a DF17 frame from the OOK FIFO chips")
{
    etl::array<uint8_t, 28> data{0, 0xa6, 0x65, 0x55, 0x69, 0x59, 0x56, 0xa6,
        0x66, 0x95, 0xa5, 0x5a, 0x95, 0x59, 0xa6, 0x69, 0x96, 0x55, 0xa5, 0x95,
        0x99, 0xa5, 0x59, 0x95, 0x69, 0x5a, 0x99, 0x6a};
    const uint8_t expected[]{0x8d, 0x40, 0x62, 0x1d, 0x58, 0xc3, 0x82, 0xd6, 0x90, 0xc8, 0xac, 0x28, 0x63, 0xa7};
    SECTION("valid chip pairs")
    {
        REQUIRE(TestLr2021::decodeOokFrame(data.data()));
        REQUIRE(etl::equal(data.begin(), data.begin() + sizeof(expected), expected));
    }
    SECTION("invalid chip pair")
    {
        data[2] = 0;
        REQUIRE_FALSE(TestLr2021::decodeOokFrame(data.data()));
    }
}
TEST_CASE("LR2021 direct FIFO read uses one transaction without dummy byte")
{
    Fixture f;
    uint8_t cmd[]{0x01, 0x02}, data[2]{};
    RadioTest::response = {0x12, 0x34};
    REQUIRE(lr20xx_hal_direct_read_fifo(&f.radio, cmd, 2, data, 2) == LR20XX_HAL_STATUS_OK);
    REQUIRE(RadioTest::transfers == std::vector<char>{'S','W','R','D'});
    REQUIRE(data[0] == 0x12);
}
TEST_CASE("HAL failures release CS and stop the operation")
{
    Fixture f;
    uint8_t command[]{1,2}, data[]{3,4};
    SECTION("short write")
    {
        RadioTest::shortTransfer = true;
        REQUIRE(lr20xx_hal_write(&f.radio, command, 2, data, 2) == LR20XX_HAL_STATUS_ERROR);
        REQUIRE(RadioTest::transfers == std::vector<char>{'S','W','D'});
    }
    SECTION("busy timeout")
    {
        RadioTest::busy = true;
        REQUIRE(lr20xx_hal_write(&f.radio, command, 2, data, 2) == LR20XX_HAL_STATUS_ERROR);
        REQUIRE(RadioTest::transfers.empty());
    }
    SECTION("missing dedicated reset")
    {
        REQUIRE(lr20xx_hal_reset(&f.radio) == LR20XX_HAL_STATUS_ERROR);
        REQUIRE(RadioTest::transfers.empty());
    }
    REQUIRE(f.radio.hasHalError());
}
TEST_CASE("FSK translation keeps the SX1262 wire format")
{
    Fixture f;
    f.radio.configure(parameters, true, 26);
    REQUIRE_FALSE(f.radio.hasHalError());
    // Verified against Semtech LR20xx command definitions: SetPacketParams / SetSyncWord.
    REQUIRE(command(0x0241) == std::vector<uint8_t>{0x02,0x41,0,16,8,0,0,52,0});
    REQUIRE(command(0x0240) == std::vector<uint8_t>{0x02,0x40,0,1,0x86,0xa0,5,0x52,0,0xc3,0x50});
    RadioTest::writes.clear();
    f.radio.configure(parameters, false, 0);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0241)[7] == 52);
    REQUIRE(command(0x0244) == std::vector<uint8_t>{2,0x44,0x22,0x33,0x44,0,0,0,0,0,0x98});
}

TEST_CASE("variable FSK length and LoRa coding rate are restored for RX")
{
    Fixture f;
    auto config = link;
    config.packetLength = 0;
    config.manchester = false;
    auto values = parameters;
    values.config = &config;
    f.radio.configure(values, false, 0);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0241)[5] == 1); // SX126x-compatible eight-bit length header
    REQUIRE(command(0x0241)[7] == 202);
    RadioTest::writes.clear();
    auto rf = GATAS::RadioParameters::DEFAULT;
    rf.mode = GATAS::Modulation::LORA;
    rf.channelBandwidth = 250000;
    values.frequency = &rf;
    values.codingRate = 7;
    f.radio.configure(values, true, 42);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0220) == std::vector<uint8_t>{2,0x20,0x75,0x30});
    REQUIRE(command(0x0221)[4] == 42);
    RadioTest::writes.clear();
    f.radio.configure(values, false, 0);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0220)[3] == 0x10);
}

TEST_CASE("unsupported RF settings fail instead of using stale settings")
{
    Fixture f;
    auto rf = GATAS::RadioParameters::DEFAULT;
    rf.gaussBt = 3;
    auto values = parameters;
    values.frequency = &rf;
    f.radio.configure(values, true, 26);
    REQUIRE(f.radio.hasHalError());
}

namespace
{
class FakeRadio : public AbstractTransceiver
{
public:
    bool failAck = false;
    int irqReads = 0;
    int acknowledgements = 0;
    int listens = 0;
    int sends = 0;
    std::vector<uint8_t> transmitted;
    explicit FakeRadio(etl::imessage_bus &bus, Spi &spi,
                       uint8_t capabilities = GATAS::LEGACY_RADIO_CAPABILITIES,
                       bool schedulerOnly = false)
        : AbstractTransceiver(bus, pins(), 0, true, false, 0, "FakeRadio", capabilities, schedulerOnly)
    {
        if (!schedulerOnly)
        {
            spiHall = &spi;
        }
    }
    bool initializeRadio() override { return true; }
    void configureRadio(const GATAS::RadioParameters &value, RadioDirection, uint8_t) override { lastRadioParameters = value; }
    void listen() override { ++listens; }
    RadioStatus standBy(Standby) override { return RadioStatus::OK; }
    etl::optional<Irq> readIrq() override { ++irqReads; return Irq{1, IRQ_TX_DONE}; }
    RadioStatus acknowledgeIrq(uint32_t) override
    {
        ++acknowledgements;
        if (failAck) { failAck = false; return RadioStatus::ERROR; }
        return RadioStatus::OK;
    }
    void receiveGFSKPacket() override {}
    void receiveLORAPacket() override {}
    void sendPacket(const GATAS::RadioParameters &, const uint8_t *data, uint8_t length, uint32_t) override
    {
        ++sends;
        transmitted.assign(data, data + length);
    }
    void fix(bool value) { hasGpsFix = value; }
    size_t queued() const { return txQueue.size(); }
    uint32_t completed() const { return statistics.transmittedPackets; }
    void run() { radioTask(nullptr); }
};
void enqueue(FakeRadio &radio, uint8_t slot = 0)
{
    auto &pool = BaseModule::getGlobalPool();
    auto data = static_cast<uint8_t *>(pool.alloc(4));
    REQUIRE(data != nullptr);
    data[0] = 0; data[1] = 0xff; data[2] = 0x55; data[3] = 0xaa;
    radio.on_receive(GATAS::RadioTxFrameMsg{pool, parameters, data, 4, slot});
}
}
TEST_CASE("shared transceiver gates TX on GPS and logical slot")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi);
    enqueue(radio);
    REQUIRE(radio.queued() == 0);
    radio.fix(true);
    enqueue(radio, 1);
    REQUIRE(radio.queued() == 0);
    enqueue(radio);
    REQUIRE(radio.queued() == 1);
}

TEST_CASE("scheduler-only radio advertises capabilities and discards radio requests")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi, 1, true);
    REQUIRE(radio.postConstruct() == GATAS::PostConstruct::OK);
    REQUIRE(radio.supportsRx(GATAS::RadioCapability::Gfsk868));
    REQUIRE(radio.supportsTx(GATAS::RadioCapability::Gfsk868));
    radio.start();
    radio.fix(true);
    xTaskNotifyCalled = false;
    f.bus.receive(GATAS::RadioControlMsg{parameters, 0});
    enqueue(radio);
    REQUIRE_FALSE(xTaskNotifyCalled);
    REQUIRE(radio.queued() == 0);
    REQUIRE(RadioTest::transfers.empty());
    etl::string<1024> status;
    etl::string_stream stream(status);
    radio.getData(stream, "");
    REQUIRE(status.find("\"schedulerOnly:b\":1") != status.npos);
}
TEST_CASE("shared task preserves Manchester encoding and retries IRQ acknowledgement once")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi);
    radio.fix(true);
    enqueue(radio);
    radio.failAck = true;
    RadioTest::notifications = {4, 1};
    REQUIRE_THROWS_AS(radio.run(), RadioTest::EndTask);
    REQUIRE(radio.sends == 1);
    REQUIRE(radio.transmitted.size() == 8);
    REQUIRE(radio.transmitted == std::vector<uint8_t>{0xaa,0xaa,0x55,0x55,0x99,0x99,0x66,0x66});
    REQUIRE(radio.completed() == 1);
    REQUIRE(radio.irqReads == 1);
    REQUIRE(radio.acknowledgements == 2);
    REQUIRE(radio.listens == 1);
}

TEST_CASE("transceiver capabilities use the physical name and apply to RX and TX")
{
    using GATAS::RadioCapability;
    Fixture f;
    f.config.capabilities = "Gfsk868, Lora868";
    TestLr2021 radio(f.bus, f.config, 1, 0);
    REQUIRE(radio.supportsRx(RadioCapability::Gfsk868));
    REQUIRE(radio.supportsRx(RadioCapability::Lora868));
    REQUIRE_FALSE(radio.supportsRx(RadioCapability::Gfsk915));
    REQUIRE_FALSE(radio.supportsTx(RadioCapability::Gfsk915));
    REQUIRE(radio.supportsTx(RadioCapability::Gfsk868));
    REQUIRE(radio.supportsTx(RadioCapability::Lora868));

    f.config.capabilities = "Gfsk868,Unknown";
    TestLr2021 invalid(f.bus, f.config, 1, 0);
    REQUIRE_FALSE(invalid.supportsRx(RadioCapability::Gfsk868));
    REQUIRE_FALSE(invalid.supportsTx(RadioCapability::Gfsk868));
}

TEST_CASE("1090 MHz OOK is opt-in and receive only on LR2021")
{
    using GATAS::RadioCapability;
    Fixture f;
    REQUIRE_FALSE(f.radio.supportsRx(RadioCapability::Gfsk868));
    REQUIRE_FALSE(f.radio.supportsRx(RadioCapability::Ook1090));
    f.config.capabilities = "Ook1090,Gfsk868";
    TestLr2021 radio(f.bus, f.config, 1, 0);
    REQUIRE(radio.supportsRx(RadioCapability::Ook1090));
    REQUIRE(radio.supportsRx(RadioCapability::Gfsk868));
    REQUIRE_FALSE(radio.supportsTx(RadioCapability::Ook1090));
}

TEST_CASE("transceiver rejects unsupported RX and TX requests")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi, static_cast<uint8_t>(GATAS::RadioCapability::Lora868));
    radio.fix(true);
    xTaskNotifyCalled = false;
    radio.on_receive(GATAS::RadioControlMsg{parameters, 0});
    REQUIRE_FALSE(xTaskNotifyCalled);
    enqueue(radio);
    REQUIRE(radio.queued() == 0);
}

#include "ace/sx1262.hpp"
namespace
{
class TestSx1262 : public Sx1262
{
public:
    explicit TestSx1262(etl::imessage_bus &bus, Spi &spi) : Sx1262(bus, pins(), 0, true, false, 0) { spiHall = &spi; }
    void configure(const GATAS::RadioParameters &parameters, bool tx, uint8_t length)
    {
        configureRadio(parameters, tx ? RadioDirection::TX : RadioDirection::RX, length);
    }
};
const std::vector<uint8_t> &sxCommand(uint8_t opcode)
{
    for (const auto &bytes : RadioTest::writes)
    {
        if (!bytes.empty() && bytes[0] == opcode) { return bytes; }
    }
    throw std::runtime_error("missing SX1262 opcode");
}
}
TEST_CASE("SX1262 still configures Manchester packet length and RX sync after extraction")
{
    Fixture f;
    TestSx1262 sx(f.bus, f.spi);
    sx.configure(parameters, true, 26);
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(sxCommand(0x8c) == std::vector<uint8_t>{0x8c,0,16,4,32,0,0,52,1,0});
    RadioTest::writes.clear();
    sx.configure(parameters, false, 0);
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(sxCommand(0x8c)[4] == 24);
    REQUIRE(sxCommand(0x8c)[7] == 52);
}
TEST_CASE("unconfigured hardware fails cleanly before accessing GPIO")
{
    etl::message_bus<8> bus;
    Sx1262 sx(bus, GATAS::PinTypeMap{}, 0, false, false, 0);
    REQUIRE(sx.postConstruct() == GATAS::PostConstruct::HARDWARE_NOT_CONFIGURED);
}

TEST_CASE("failed transceivers can be deleted through BaseModule while preserving failure status")
{
    etl::message_bus<8> bus;
    Config config(bus);
    REQUIRE_FALSE(static_cast<const BaseModule &>(config).canDelete());

    BaseModule *module = nullptr;
    SECTION("SX1262")
    {
        module = new Sx1262(bus, GATAS::PinTypeMap{}, 0, false, false, 0);
    }
    if constexpr (!Lr2021::SCHEDULER_ONLY)
    {
        SECTION("LR2021")
        {
            module = new Lr2021(bus, config, 0, 0);
        }
    }

    const auto name = module->name();
    const auto result = module->postConstruct();
    REQUIRE(result != GATAS::PostConstruct::OK);
    BaseModule::setModuleStatus(name, result);
    REQUIRE(module->canDelete());
    delete module;
    REQUIRE(BaseModule::registeredModules().at(name).module == nullptr);
    REQUIRE(BaseModule::registeredModules().at(name).result == result);
}

TEST_CASE("LR2021 scheduler-only switch bypasses hardware discovery")
{
    if constexpr (Lr2021::SCHEDULER_ONLY)
    {
        Fixture f;
        RadioTest::busy = true;
        REQUIRE(f.radio.postConstruct() == GATAS::PostConstruct::OK);
        REQUIRE(RadioTest::transfers.empty());
    }
}

TEST_CASE("mixed transceivers with matching configuration indices have distinct runtime identities")
{
    etl::message_bus<8> bus;
    Config config(bus);
    RadioModuleRegistration sxRegistration(Sx1262::NAMES[0]);
    RadioModuleRegistration lrRegistration(Lr2021::NAMES[0]);
    Sx1262 sx(bus, config, 0, 0);
    Lr2021 lr(bus, config, 0, 1);
    REQUIRE(GATAS::PostConstruct::OK == sx.startAndRegister(Sx1262::NAMES[0]));
    REQUIRE(GATAS::PostConstruct::OK == lr.startAndRegister(Lr2021::NAMES[0]));
    REQUIRE(sx.radio() == 0);
    REQUIRE(lr.radio() == 1);
    REQUIRE(BaseModule::moduleByName(config, Radio::NAMES[0]) == &sx);
    REQUIRE(BaseModule::moduleByName(config, Radio::NAMES[1]) == &lr);
}

TEST_CASE("failed radio identity can be reused by a different transceiver configuration")
{
    etl::message_bus<8> bus;
    Config config(bus);
    auto *failed = new Sx1262(bus, GATAS::PinTypeMap{}, 0, false, false, 0);
    REQUIRE(failed->postConstruct() == GATAS::PostConstruct::HARDWARE_NOT_CONFIGURED);
    delete failed;
    REQUIRE(BaseModule::moduleByName(config, Radio::NAMES[0]) == nullptr);
    RadioModuleRegistration replacementRegistration(Lr2021::NAMES[1]);
    Lr2021 replacement(bus, config, 1, 0);
    REQUIRE(GATAS::PostConstruct::OK == replacement.startAndRegister(Lr2021::NAMES[1]));
    REQUIRE(replacement.radio() == 0);
    REQUIRE(BaseModule::moduleByName(config, Radio::NAMES[0]) == &replacement);
}
