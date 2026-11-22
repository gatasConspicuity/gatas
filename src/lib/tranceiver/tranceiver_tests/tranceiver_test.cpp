#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <map>
#include <string>
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
    GATAS::ConfigString hardware = "PICO_2RADIO";
    etl::optional<GATAS::ConfigString> customCapabilities;
    GATAS::ConfigString capabilities = "Gfsk868,Lora868,Gfsk915";
    std::map<std::string, int> values;
    mutable std::vector<std::string> valueReads;
    int valueByPath(int fallback, etl::string_view name, etl::string_view key) const override
    {
        const auto path = std::string(name.data(), name.size()) + "/" + std::string(key.data(), key.size());
        valueReads.push_back(path);
        if (const auto found = values.find(path); found != values.end())
        {
            return found->second;
        }
        return name == "Lr2021_1" && key == "txEnabled" ? 1 : fallback;
    }
    const GATAS::ConfigString strValueByPath(etl::string_view fallback, etl::string_view name,
                                            etl::string_view key) const override
    {
        if (name == "hardware" && key == "type")
        {
            return hardware;
        }
        if (name == "PICO_2RADIO/Lr2021_0" && key == "capabilities" && customCapabilities)
        {
            return *customCapabilities;
        }
        GATAS::ConfigString radioPath(hardware);
        radioPath.append("/Lr2021_1");
        if (name == radioPath && key == "capabilities")
        {
            return capabilities;
        }
        if (name == "Lr2021_0" || name == "Lr2021_1")
        {
            return GATAS::ConfigString("Ook1090"); // Stale top-level settings must be ignored.
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
    using Lr2021::listen;
    using Lr2021::receiveOOKPacket;
    using Lr2021::receiveGFSKPacket;
    using Lr2021::receiveLORAPacket;
    bool serviceIrq() { return readIrq().has_value(); }
    uint32_t received() const { return statistics.receivedPackets; }
    uint32_t offset() const { return offsetHz; }
    static int hardwareValue(const Configuration &config, etl::string_view name, etl::string_view key, int fallback)
    {
        return readHardwareValue(config, name, key, fallback);
    }
    void attach(SpiModule &spi, RxDataFrameQueue &queue) { spiHall = &spi; rxDataFrameQueue = &queue; }
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
    RxDataFrameQueue queue{bus, config};
    TestLr2021 radio{bus, config, 0, 0};
    Fixture() { RadioTest::reset(); radio.attach(spi, queue); xSemaphoreTakeValue = pdTRUE; }
};
const std::vector<uint8_t> &command(uint16_t opcode)
{
    for (const auto &bytes : RadioTest::writes)
    {
        if (bytes.size() >= 2 && bytes[0] == (opcode >> 8) && bytes[1] == (opcode & 255)) { return bytes; }
    }
    throw std::runtime_error("missing opcode");
}

void lrReply(std::initializer_list<uint8_t> data)
{
    RadioTest::response.insert(RadioTest::response.end(), {0, 0});
    RadioTest::response.insert(RadioTest::response.end(), data);
}

void lrPacketCount(uint16_t count, bool lora = false)
{
    std::vector<uint8_t> bytes(lora ? 12 : 16, 0);
    bytes[2] = static_cast<uint8_t>(count >> 8);
    bytes[3] = static_cast<uint8_t>(count);
    RadioTest::response.insert(RadioTest::response.end(), bytes.begin(), bytes.end());
}

size_t commandCount(uint16_t opcode)
{
    return std::count_if(RadioTest::writes.begin(), RadioTest::writes.end(), [opcode](const auto &bytes)
    {
        return bytes.size() >= 2 && bytes[0] == (opcode >> 8) && bytes[1] == (opcode & 255);
    });
}
}

TEST_CASE("LR2021 reads use separate command and response CS transactions")
{
    Fixture f;
    SECTION("GetErrors reads the error flags after both status bytes")
    {
        RadioTest::response = {0x04, 0x21, 0x00, 0x40};
        lr20xx_system_errors_t errors = 0;
        REQUIRE(LR20XX_STATUS_OK == lr20xx_system_get_errors(&f.radio, &errors));
        REQUIRE(0x0040 == errors);
        REQUIRE(command(0x0110) == std::vector<uint8_t>{0x01, 0x10});
    }
    SECTION("GetVersion reads both firmware version bytes")
    {
        RadioTest::response = {0x04, 0x21, 0x01, 0x0d};
        lr20xx_system_version_t version{};
        REQUIRE(LR20XX_STATUS_OK == lr20xx_system_get_version(&f.radio, &version));
        REQUIRE(0x01 == version.major);
        REQUIRE(0x0d == version.minor);
        REQUIRE(command(0x0101) == std::vector<uint8_t>{0x01, 0x01});
    }
    REQUIRE(RadioTest::transfers == std::vector<char>{'S','W','D','S','R','R','D'});
    REQUIRE(RadioTest::reads == std::vector<size_t>{2, 2});
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
    REQUIRE(std::vector<uint8_t>{2, 0x1a, 13} == command(0x021a));
    for (const auto &write : RadioTest::writes)
    {
        if (write.size() >= 5 && write[0] == 1 && write[1] == 5)
        {
            REQUIRE_FALSE((write[2] == 0xf3 && write[3] == 0x0e && write[4] == 0x14));
        }
    }
    RadioTest::writes.clear();
    f.radio.configure(profile, false, 0);
    REQUIRE(RadioTest::writes.empty());
    f.radio.configure(profile, true, 14);
    REQUIRE(f.radio.hasHalError());
    REQUIRE(RadioTest::writes.empty());
    f.radio.configure(parameters, false, 0);
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(command(0x0105) == std::vector<uint8_t>{1, 5, 0xf3, 1, 0x50, 0, 0, 0, 0xff, 0, 0, 0, 0});
    REQUIRE(std::vector<uint8_t>{2, 0x1a, 0} == command(0x021a));
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

TEST_CASE("LR2021 drains complete OOK captures and leaves partial captures for the next completion")
{
    Fixture f;
    const GATAS::RfConfig rf{GATAS::Modulation::OOK, 1'090'000'000, 0, 0, 3'076'000, 2'000'000, 0, 0, GATAS::RadioCapability::Ook1090};
    const GATAS::LinkLayerConfig adsb{10, GATAS::DataSource::ADSB, false, 14, 0, 0, 0, {}};
    f.radio.configure({&adsb, &rf, rf.baseFrequency, 0}, false, 0);
    f.radio.listen();
    REQUIRE(std::vector<uint8_t>{2, 0x0c, 0xff, 0xff, 0xff} == command(0x020c));
    RadioTest::reset();
    const std::vector<uint8_t> capture{0xa6, 0x65, 0x55, 0x69, 0x59, 0x56, 0xa6,
        0x66, 0x95, 0xa5, 0x5a, 0x95, 0x59, 0xa6, 0x69, 0x96, 0x55, 0xa5, 0x95,
        0x99, 0xa5, 0x59, 0x95, 0x69, 0x5a, 0x99, 0x6a};
    auto first = capture;
    bool invalidFirst = false;
    SECTION("two valid captures") {}
    SECTION("an invalid capture does not block the next one")
    {
        first[1] = 0;
        invalidFirst = true;
    }
    lrReply({0, 27, 80, 80, 0, 0});
    lrReply({0, 58}); // Two full captures and four bytes of the next one.
    RadioTest::response.insert(RadioTest::response.end(), first.begin(), first.end());
    RadioTest::response.insert(RadioTest::response.end(), capture.begin(), capture.end());
    f.radio.receiveOOKPacket();
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE((invalidFirst ? 1 : 2) == f.radio.received());
    REQUIRE(2 == commandCount(0x0001));
    REQUIRE(0 == commandCount(0x011e)); // No FIFO clear between captures.
    REQUIRE(0 == commandCount(0x020c)); // No RX restart.
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
}

TEST_CASE("LR2021 drains fixed GFSK captures without consuming a partial packet")
{
    Fixture f;
    f.radio.configure(parameters, false, 0);
    f.radio.listen();
    RadioTest::reset();
    lrReply({0, 52, 80, 80, 0, 0});
    lrReply({0, 110}); // Two 52-byte Manchester captures, six partial bytes.
    RadioTest::response.resize(RadioTest::response.size() + 104, 0x55);
    f.radio.receiveGFSKPacket();
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(2 == f.radio.received());
    REQUIRE(2 == commandCount(0x0001));
    REQUIRE(0 == commandCount(0x011e));
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
}

TEST_CASE("LR2021 preserves variable packet boundaries or recovers when metadata is lost")
{
    Fixture f;
    auto config = link;
    config.packetLength = 0;
    config.manchester = false;
    auto profile = parameters;
    profile.config = &config;
    f.radio.configure(profile, false, 0);
    f.radio.listen();
    RadioTest::reset();
    SECTION("different packet lengths are consumed separately")
    {
        lrPacketCount(1);
        lrReply({0, 6, 80, 80, 0, 0});
        lrPacketCount(1);
        lrReply({0, 6});
        RadioTest::response.insert(RadioTest::response.end(), {1, 2, 3, 4, 5, 6});
        f.radio.receiveGFSKPacket();
        REQUIRE_FALSE(f.radio.hasHalError());
        lrPacketCount(2);
        lrReply({0, 10, 80, 80, 0, 0});
        lrPacketCount(2);
        lrReply({0, 10});
        RadioTest::response.resize(RadioTest::response.size() + 10, 0x55);
        f.radio.receiveGFSKPacket();
        REQUIRE_FALSE(f.radio.hasHalError());
        REQUIRE(2 == f.radio.received());
        REQUIRE(2 == commandCount(0x0001));
        REQUIRE(0 == commandCount(0x020c));
        REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
    }
    SECTION("coalesced completions cannot apply the latest length to an older packet")
    {
        lrPacketCount(2);
        lrReply({0, 10, 80, 80, 0, 0});
        lrPacketCount(2);
        f.radio.receiveGFSKPacket();
        REQUIRE(f.radio.hasHalError());
        REQUIRE(0 == f.radio.received());
        REQUIRE(0 == commandCount(0x0001));
    }
    SECTION("a completion during the metadata read requires recovery")
    {
        lrPacketCount(1);
        lrReply({0, 6, 80, 80, 0, 0});
        lrPacketCount(2);
        f.radio.receiveGFSKPacket();
        REQUIRE(f.radio.hasHalError());
        REQUIRE(0 == commandCount(0x0001));
    }
}

TEST_CASE("LR2021 FIFO overflow requests recovery before reading corrupted boundaries")
{
    Fixture f;
    RadioTest::response = {0, 0, 0, 0, 0, 1}; // FIFO RX IRQ.
    lrReply({0x10, 0}); // Overflow.
    REQUIRE_FALSE(f.radio.serviceIrq());
    REQUIRE(f.radio.hasHalError());
    REQUIRE(0 == commandCount(0x0001));
    f.radio.configure(parameters, false, 0);
    f.radio.listen();
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(1 == commandCount(0x011e));
    REQUIRE(std::vector<uint8_t>{2, 0x0c, 0xff, 0xff, 0xff} == command(0x020c));
}

TEST_CASE("LR2021 explicit LoRa drains a rejected packet before accepting the next one")
{
    Fixture f;
    auto config = link;
    config.packetLength = 0;
    config.manchester = false;
    auto rf = *parameters.frequency;
    rf.mode = GATAS::Modulation::LORA;
    rf.channelBandwidth = 250'000;
    auto profile = parameters;
    profile.config = &config;
    profile.frequency = &rf;
    f.radio.configure(profile, false, 0);
    f.radio.listen();
    RadioTest::reset();
    RadioTest::response = {0, 0, 0, 0x44, 0, 0}; // CRC_ERROR + RX_DONE.
    REQUIRE(f.radio.serviceIrq());
    lrPacketCount(1, true);
    lrReply({0, 4, 0, 80, 80, 0, 0, 0, 0});
    lrPacketCount(1, true);
    lrReply({0, 4});
    RadioTest::response.insert(RadioTest::response.end(), {1, 2, 3, 4});
    f.radio.receiveLORAPacket();
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(0 == f.radio.received());
    REQUIRE(1 == commandCount(0x0001)); // Bad CRC bytes were still consumed.

    RadioTest::response.insert(RadioTest::response.end(), {0, 0, 0, 4, 0, 0}); // RX_DONE only.
    REQUIRE(f.radio.serviceIrq());
    lrPacketCount(2, true);
    lrReply({0, 6, 0, 80, 80, 0, 0, 0, 0});
    lrPacketCount(2, true);
    lrReply({0, 6});
    RadioTest::response.insert(RadioTest::response.end(), {1, 2, 3, 4, 5, 6});
    f.radio.receiveLORAPacket();
    REQUIRE_FALSE(f.radio.hasHalError());
    REQUIRE(1 == f.radio.received());
    REQUIRE(2 == commandCount(0x0001));
    REQUIRE(0 == commandCount(0x020c));
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
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
    int standbys = 0;
    int configurations = 0;
    std::vector<uint8_t> irqEvents;
    size_t irqIndex = 0;
    std::vector<uint32_t> receivedFrequencies;
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
    void configureRadio(const GATAS::RadioParameters &value, RadioDirection, uint8_t) override { ++configurations; lastRadioParameters = value; }
    void listen() override { ++listens; }
    RadioStatus standBy(Standby) override { ++standbys; return RadioStatus::OK; }
    etl::optional<Irq> readIrq() override
    {
        ++irqReads;
        return Irq{1, irqIndex < irqEvents.size() ? irqEvents[irqIndex++] : static_cast<uint8_t>(IRQ_TX_DONE)};
    }
    RadioStatus acknowledgeIrq(uint32_t) override
    {
        ++acknowledgements;
        if (failAck) { failAck = false; return RadioStatus::ERROR; }
        return RadioStatus::OK;
    }
    void receiveGFSKPacket() override { receivedFrequencies.push_back(lastRadioParameters.hopFrequency); }
    void receiveLORAPacket() override { receivedFrequencies.push_back(lastRadioParameters.hopFrequency); }
    void receiveOOKPacket() override { receivedFrequencies.push_back(lastRadioParameters.hopFrequency); }
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

TEST_CASE("RX packet completion keeps the scheduled slot listening for every modem")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi, 0xff);
    auto rf = *parameters.frequency;
    SECTION("GFSK") { rf.mode = GATAS::Modulation::GFSK; }
    SECTION("LoRa") { rf.mode = GATAS::Modulation::LORA; }
    SECTION("OOK") { rf.mode = GATAS::Modulation::OOK; }
    auto profile = parameters;
    profile.frequency = &rf;
    radio.on_receive(GATAS::RadioControlMsg{profile, 0});
    RadioTest::notifications = {8, 1, 1};
    radio.irqEvents = {1, 1};
    REQUIRE_THROWS_AS(radio.run(), RadioTest::EndTask);
    REQUIRE(2 == radio.receivedFrequencies.size());
    REQUIRE(1 == radio.listens);
    REQUIRE(1 == radio.configurations);
    REQUIRE(0 == radio.standbys);
}

TEST_CASE("continuous RX drains the old slot before a frequency hop and resumes after TX")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi);
    radio.fix(true);
    auto next = parameters;
    next.hopFrequency += 200'000;
    radio.on_receive(GATAS::RadioControlMsg{parameters, 0});
    RadioTest::notifications = {8, 1, 8, 1, 4, 1, 1};
    radio.irqEvents = {1, 1, 1, 1, 2, 1};
    RadioTest::onWake = [&](size_t index)
    {
        if (index == 2)
        {
            radio.on_receive(GATAS::RadioControlMsg{next, 0});
        }
        if (index == 4)
        {
            enqueue(radio);
        }
    };
    REQUIRE_THROWS_AS(radio.run(), RadioTest::EndTask);
    REQUIRE(std::vector<uint32_t>{parameters.hopFrequency, parameters.hopFrequency,
                                 next.hopFrequency, next.hopFrequency, next.hopFrequency} == radio.receivedFrequencies);
    REQUIRE(2 == radio.standbys);
    REQUIRE(3 == radio.listens);
    REQUIRE(1 == radio.sends);
    REQUIRE(1 == radio.completed());
}

TEST_CASE("continuous RX preserves a packet across an IRQ acknowledgement failure")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi);
    radio.on_receive(GATAS::RadioControlMsg{parameters, 0});
    radio.failAck = true;
    radio.irqEvents = {1};
    RadioTest::notifications = {8, 1};
    REQUIRE_THROWS_AS(radio.run(), RadioTest::EndTask);
    REQUIRE(1 == radio.receivedFrequencies.size());
    REQUIRE(1 == radio.irqReads);
    REQUIRE(2 == radio.acknowledgements);
    REQUIRE(2 == radio.listens);
}

TEST_CASE("a receive slot change during TX is applied after transmission completes")
{
    Fixture f;
    FakeRadio radio(f.bus, f.spi);
    radio.fix(true);
    enqueue(radio);
    auto next = parameters;
    next.hopFrequency += 200'000;
    RadioTest::notifications = {4, 8, 1, 1};
    radio.irqEvents = {2, 1};
    RadioTest::onWake = [&](size_t index)
    {
        if (index == 1)
        {
            radio.on_receive(GATAS::RadioControlMsg{next, 0});
        }
    };
    REQUIRE_THROWS_AS(radio.run(), RadioTest::EndTask);
    REQUIRE(1 == radio.sends);
    REQUIRE(1 == radio.completed());
    REQUIRE(1 == radio.listens);
    REQUIRE(2 == radio.configurations);
    REQUIRE(std::vector<uint32_t>{next.hopFrequency} == radio.receivedFrequencies);
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

TEST_CASE("transceiver capabilities use the hardware profile and physical name for RX and TX")
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

TEST_CASE("LR2021 capabilities can differ by board profile")
{
    using GATAS::RadioCapability;
    Fixture f;
    f.config.customCapabilities = GATAS::ConfigString("Gfsk868,Lora868,Gfsk915,Ook1090");
    f.config.capabilities = "Gfsk868,Ook1090";

    SECTION("Custom board enables 1090 MHz reception on radio zero")
    {
        f.config.hardware = "PICO_2RADIO";
        TestLr2021 customRadio(f.bus, f.config, 0, 0);
        REQUIRE(customRadio.supportsRx(RadioCapability::Ook1090));
        REQUIRE(customRadio.supportsRx(RadioCapability::Gfsk868));
        REQUIRE_FALSE(customRadio.supportsTx(RadioCapability::Ook1090));
    }

    SECTION("Dual board uses its own radio capability lists")
    {
        f.config.hardware = "GATAS_DUAL_LR2021";
        TestLr2021 otherRadio(f.bus, f.config, 0, 0);
        TestLr2021 adsbRadio(f.bus, f.config, 1, 1);
        REQUIRE_FALSE(otherRadio.supportsRx(RadioCapability::Ook1090));
        REQUIRE(adsbRadio.supportsRx(RadioCapability::Ook1090));
    }
}

TEST_CASE("LR2021 board settings use the selected profile while calibration and TX stay per device")
{
    Fixture f;
    f.config.customCapabilities = GATAS::ConfigString("Gfsk868,Ook1090");
    f.config.values = {
        {"Lr2021_0/offset", 250},
        {"Lr2021_0/txEnabled", 1},
        {"Lr2021_0/irqDio", 5},
        {"Lr2021_0/tcxoVoltage", 6},
        {"Lr2021_0/tcxoStartupUs", 10},
        {"Lr2021_0/rxSwitchDio", 5},
        {"Lr2021_0/txSwitchDio", 5},
        {"PICO_2RADIO/Lr2021_0/offset", 999},
        {"PICO_2RADIO/Lr2021_0/txEnabled", 0},
        {"PICO_2RADIO/Lr2021_0/irqDio", 7},
        {"PICO_2RADIO/Lr2021_0/tcxoVoltage", -1},
        {"PICO_2RADIO/Lr2021_0/tcxoStartupUs", 5000},
        {"PICO_2RADIO/Lr2021_0/rxSwitchDio", 8},
        {"PICO_2RADIO/Lr2021_0/txSwitchDio", 9},
        {"GATAS_DUAL_LR2021/Lr2021_0/tcxoVoltage", 6}};
    f.config.valueReads.clear();
    TestLr2021 radio(f.bus, f.config, 0, 0);
    REQUIRE(250 == radio.offset());
    REQUIRE(radio.supportsTx(GATAS::RadioCapability::Gfsk868));
    for (const auto key : {"irqDio", "tcxoVoltage", "tcxoStartupUs", "rxSwitchDio", "txSwitchDio"})
    {
        const auto boardPath = std::string("PICO_2RADIO/Lr2021_0/") + key;
        const auto oldPath = std::string("Lr2021_0/") + key;
        REQUIRE(f.config.valueReads.end() != std::find(f.config.valueReads.begin(), f.config.valueReads.end(), boardPath));
        REQUIRE(f.config.valueReads.end() == std::find(f.config.valueReads.begin(), f.config.valueReads.end(), oldPath));
    }
    REQUIRE(-1 == TestLr2021::hardwareValue(f.config, "Lr2021_0", "tcxoVoltage", 99));
    REQUIRE(8 == TestLr2021::hardwareValue(f.config, "Lr2021_0", "rxSwitchDio", -1));
    f.config.hardware = "GATAS_DUAL_LR2021";
    REQUIRE(6 == TestLr2021::hardwareValue(f.config, "Lr2021_0", "tcxoVoltage", 99));
    REQUIRE(-1 == TestLr2021::hardwareValue(f.config, "Lr2021_0", "rxSwitchDio", -1));

    f.config.values["Lr2021_0/offset"] = 500;
    f.config.values["Lr2021_0/txEnabled"] = 0;
    radio.on_receive(GATAS::ConfigUpdatedMsg{f.config, etl::string_view("Lr2021_0")});
    REQUIRE(500 == radio.offset());
    REQUIRE_FALSE(radio.supportsTx(GATAS::RadioCapability::Gfsk868));
}

TEST_CASE("hardware settings never fall back to stale top-level radio settings")
{
    Fixture f;
    f.config.values["Lr2021_0/tcxoVoltage"] = 6;
    f.config.customCapabilities.reset();
    REQUIRE(-1 == TestLr2021::hardwareValue(f.config, "Lr2021_0", "tcxoVoltage", -1));
    REQUIRE(0 == AbstractTransceiver::readCapabilities(f.config, "Lr2021_0", 0xff));
    f.config.hardware.clear();
    REQUIRE(-1 == TestLr2021::hardwareValue(f.config, "Lr2021_0", "tcxoVoltage", -1));
    REQUIRE(0 == AbstractTransceiver::readCapabilities(f.config, "Lr2021_0", 0xff));
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
    using Sx1262::listen;
    using Sx1262::receiveGFSKPacket;
    using Sx1262::receiveLORAPacket;
    void attachQueue(RxDataFrameQueue &queue) { rxDataFrameQueue = &queue; }
    uint32_t received() const { return statistics.receivedPackets; }
    bool pendingReceive() { const auto irq = readIrq(); return irq && (irq->events & IRQ_RX_DONE); }
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

namespace
{
void sxCounts(uint16_t received, uint16_t crcErrors = 0)
{
    RadioTest::response.insert(RadioTest::response.end(), {
        static_cast<uint8_t>(received >> 8), static_cast<uint8_t>(received),
        static_cast<uint8_t>(crcErrors >> 8), static_cast<uint8_t>(crcErrors), 0, 0});
}

void sxCapture(uint16_t before, uint16_t after, uint8_t pointer, bool lora = false,
               bool badCrc = false, uint16_t historicalCrcErrors = 0)
{
    const uint16_t crcErrors = historicalCrcErrors + (badCrc ? 1 : 0);
    sxCounts(before, crcErrors);
    if (lora)
    {
        RadioTest::response.insert(RadioTest::response.end(), {0, static_cast<uint8_t>(badCrc ? 0x40 : 0)});
        RadioTest::response.insert(RadioTest::response.end(), {160, 0, 160});
    }
    else
    {
        RadioTest::response.insert(RadioTest::response.end(), {2, 160, 160});
    }
    RadioTest::response.insert(RadioTest::response.end(), {8, pointer});
    if (!lora || !badCrc)
    {
        RadioTest::response.resize(RadioTest::response.size() + 8, 0x55);
    }
    sxCounts(after, crcErrors);
}
}

TEST_CASE("SX1262 continuous RX follows the circular buffer and ignores stale completion notifications")
{
    Fixture f;
    TestSx1262 sx(f.bus, f.spi);
    sx.attachQueue(f.queue);
    auto profile = parameters;
    auto rf = *profile.frequency;
    bool lora = false;
    SECTION("GFSK") {}
    SECTION("LoRa")
    {
        lora = true;
        rf.mode = GATAS::Modulation::LORA;
        rf.channelBandwidth = 250'000;
    }
    profile.frequency = &rf;
    sx.configure(profile, false, 0);
    sx.listen();
    REQUIRE(std::vector<uint8_t>{0x82, 0xff, 0xff, 0xff} == sxCommand(0x82));
    RadioTest::reset();
    sxCapture(1, 1, 250, lora);
    sxCapture(2, 2, 2, lora); // Next packet wraps around the 256-byte buffer.
    sxCounts(2); // A queued notification for a packet already read.
    for (uint8_t i = 0; i < 3; ++i)
    {
        if (lora)
        {
            sx.receiveLORAPacket();
        }
        else
        {
            sx.receiveGFSKPacket();
        }
    }
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(2 == sx.received());
    std::vector<uint8_t> pointers;
    for (const auto &write : RadioTest::writes)
    {
        REQUIRE(0x82 != write[0]); // No RX restart during packet reads.
        if (write[0] == 0x1e)
        {
            pointers.push_back(write[1]);
        }
    }
    REQUIRE(std::vector<uint8_t>{250, 2} == pointers);
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
}

TEST_CASE("SX1262 retries a packet read if a new completion changes its metadata")
{
    Fixture f;
    TestSx1262 sx(f.bus, f.spi);
    sx.attachQueue(f.queue);
    sx.configure(parameters, false, 0);
    sx.listen();
    RadioTest::reset();
    sxCapture(1, 2, 54);
    sxCapture(2, 2, 62);
    sx.receiveGFSKPacket();
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(1 == sx.received());
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
}

TEST_CASE("SX1262 bounds retries and retains work when IRQ completions coalesce")
{
    Fixture f;
    TestSx1262 sx(f.bus, f.spi);
    sx.attachQueue(f.queue);
    sx.configure(parameters, false, 0);
    sx.listen();
    RadioTest::reset();
    sxCapture(1, 2, 54);
    sxCapture(2, 3, 62);
    sx.receiveGFSKPacket();
    REQUIRE(0 == sx.received());
    REQUIRE(sx.pendingReceive()); // No hardware IRQ is required for the retry.
    sxCapture(3, 3, 70);
    sx.receiveGFSKPacket();
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(1 == sx.received());
}

TEST_CASE("SX1262 rejects LoRa IRQ CRC errors but accepts historical CRC counts")
{
    Fixture f;
    TestSx1262 sx(f.bus, f.spi);
    sx.attachQueue(f.queue);
    auto profile = parameters;
    auto rf = *profile.frequency;
    rf.mode = GATAS::Modulation::LORA;
    rf.channelBandwidth = 250'000;
    profile.frequency = &rf;
    sx.configure(profile, false, 0);
    sx.listen();
    RadioTest::reset();
    SECTION("CRC error is still latched in hardware")
    {
        sxCapture(1, 1, 54, true, true);
        sx.receiveLORAPacket();
        REQUIRE(0 == sx.received());
    }
    SECTION("CRC error was already acknowledged by the radio task")
    {
        RadioTest::response = {0, 0x42}; // RX_DONE | CRC_ERROR.
        REQUIRE(sx.pendingReceive());
        sxCapture(1, 1, 54, true, true);
        // The hardware IRQ was cleared, so only the saved IRQ marks it bad.
        RadioTest::response[8] = 0;
        RadioTest::response[9] = 0;
        sx.receiveLORAPacket();
        REQUIRE(0 == sx.received());
    }
    SECTION("a past CRC error does not reject the latest good packet")
    {
        sxCapture(2, 2, 54, true, false, 1);
        sx.receiveLORAPacket();
        REQUIRE(1 == sx.received());
    }
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
}

TEST_CASE("SX1262 GFSK ignores the unused hardware CRC counter")
{
    Fixture f;
    TestSx1262 sx(f.bus, f.spi);
    sx.attachQueue(f.queue);
    sx.configure(parameters, false, 0);
    sx.listen();
    RadioTest::reset();
    sxCapture(1, 1, 54, false, false, 1);
    sx.receiveGFSKPacket();
    REQUIRE_FALSE(sx.hasHalError());
    REQUIRE(1 == sx.received());
    REQUIRE(RadioTest::response.size() == RadioTest::responseOffset);
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
