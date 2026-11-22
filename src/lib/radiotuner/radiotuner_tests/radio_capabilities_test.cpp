#include <catch2/catch_test_macros.hpp>

#include "mockconfig.h"
#include "etl/message_bus.h"

#define private public
#include "../ace/radiotunerrx_v2.hpp"
#include "../ace/radiotunertx_v2.hpp"
#undef private

namespace
{
using GATAS::ConspicuityProtocol;
using GATAS::ProtocolMode;
using GATAS::RadioCapability;

class TestRadio : public Radio
{
    const uint8_t slot;
    const RadioCapability rx;
    const RadioCapability tx;

public:
    bool txEnabled = true;

    TestRadio(etl::imessage_bus &bus, uint8_t slot, RadioCapability rx, RadioCapability tx)
        : Radio(bus, Radio::NAMES[slot]), slot(slot), rx(rx), tx(tx) {}

    uint8_t radio() const override { return slot; }
    bool supportsRx(RadioCapability capability) const override { return rx == capability; }
    bool supportsTx(RadioCapability capability) const override { return txEnabled && tx == capability; }
    GATAS::PostConstruct postConstruct() override { return GATAS::PostConstruct::OK; }
    void start() override {}
};
}

TEST_CASE("RX assigns mixed radios and fills gaps only with compatible profiles")
{
    etl::message_bus<8> bus;
    MockConfig config(bus);
    GATAS::OwnshipState ownship;
    TestRadio lora(bus, 0, RadioCapability::Lora868, RadioCapability::Lora868);
    TestRadio gfsk(bus, 1, RadioCapability::Gfsk868, RadioCapability::Gfsk868);
    RadioTunerRx tuner(bus, config, ownship);
    tuner.radioCtxList.emplace_back(&tuner, lora);
    tuner.radioCtxList.emplace_back(&tuner, gfsk);
    tuner.currentZone.set(CountryRegulations::Zone::ZONE1);
    tuner.configuredProtocols = {
        {ConspicuityProtocol::FLARM, ProtocolMode::RX},
        {ConspicuityProtocol::FANET, ProtocolMode::RX}};

    etl::array<uint8_t, GATAS::RADIO_PROTOCOLS.size()> boost{};
    tuner.assignProtocolsImpl(boost);

    REQUIRE(tuner.radioCtxList[0].protocolTimings.size() == 1);
    REQUIRE(tuner.radioCtxList[0].protocolTimings[0].protocolTimeConfig->radioConfig.protocol() == ConspicuityProtocol::FANET);
    bool hasFlarm = false;
    bool hasGap = false;
    for (const auto &entry : tuner.radioCtxList[1].protocolTimings)
    {
        if (entry.protocolTimeConfig == nullptr)
        {
            hasGap = true;
        }
        else
        {
            REQUIRE(entry.protocolTimeConfig->radioConfig.protocol() == ConspicuityProtocol::FLARM);
            hasFlarm = true;
        }
    }
    REQUIRE(hasFlarm);
    REQUIRE(hasGap);

    tuner.radioCtxList.clear();
    tuner.radioCtxList.emplace_back(&tuner, lora);
    tuner.configuredProtocols = {{ConspicuityProtocol::FLARM, ProtocolMode::RX}};
    tuner.assignProtocolsImpl(boost);
    REQUIRE(tuner.radioCtxList[0].protocolTimings.empty());
}

TEST_CASE("Three mixed radios can schedule RX and select the third radio for TX")
{
    etl::message_bus<8> bus;
    MockConfig config(bus);
    GATAS::OwnshipState ownship;
    TestRadio lora(bus, 0, RadioCapability::Lora868, RadioCapability::Lora868);
    TestRadio ook(bus, 1, RadioCapability::Ook1090, RadioCapability::Ook1090);
    ook.txEnabled = false;
    TestRadio gfsk(bus, 2, RadioCapability::Gfsk868, RadioCapability::Gfsk868);
    RadioTunerRx rx(bus, config, ownship);
    rx.radioCtxList.emplace_back(&rx, lora);
    rx.radioCtxList.emplace_back(&rx, ook);
    rx.radioCtxList.emplace_back(&rx, gfsk);
    rx.currentZone.set(CountryRegulations::Zone::ZONE1);
    rx.configuredProtocols = {
        {ConspicuityProtocol::FANET, ProtocolMode::RX},
        {ConspicuityProtocol::ADSB, ProtocolMode::RX},
        {ConspicuityProtocol::FLARM, ProtocolMode::RX}};

    etl::array<uint8_t, GATAS::RADIO_PROTOCOLS.size()> boost{};
    rx.assignProtocolsImpl(boost);

    const etl::array<ConspicuityProtocol, 3> expected{ConspicuityProtocol::FANET, ConspicuityProtocol::ADSB, ConspicuityProtocol::FLARM};
    REQUIRE(3 == rx.radioCtxList.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        bool assigned = false;
        for (const auto &entry : rx.radioCtxList[i].protocolTimings)
        {
            if (entry.protocolTimeConfig != nullptr)
            {
                REQUIRE(expected[i] == entry.protocolTimeConfig->radioConfig.protocol());
                assigned = true;
            }
        }
        REQUIRE(assigned);
    }

    RadioTunerTx tx(bus, config, ownship);
    tx.radios[0] = &lora;
    tx.radios[1] = &ook;
    tx.radios[2] = &gfsk;
    REQUIRE(2 == tx.selectRadio(ConspicuityProtocol::FLARM, RadioCapability::Gfsk868));
}

TEST_CASE("TX falls back when the RX assignment cannot transmit the requested profile")
{
    etl::message_bus<8> bus;
    MockConfig config(bus);
    GATAS::OwnshipState ownship;
    TestRadio rx(bus, 0, RadioCapability::Gfsk868, RadioCapability::Lora868);
    TestRadio tx(bus, 1, RadioCapability::Lora868, RadioCapability::Gfsk868);
    RadioTunerTx tuner(bus, config, ownship);
    tuner.radios[0] = &rx;
    tuner.radios[1] = &tx;

    REQUIRE(tuner.selectRadio(ConspicuityProtocol::FLARM, RadioCapability::Gfsk868) == 1);
    tuner.on_receive(GATAS::RadioControlMsg{
        {&CountryRegulations::PROTOCOL_FLARM, &CountryRegulations::Europe_m, 868'200'000, 0}, 0});
    REQUIRE(tuner.selectRadio(ConspicuityProtocol::FLARM, RadioCapability::Gfsk868) == 1);
    REQUIRE_FALSE(tuner.selectRadio(ConspicuityProtocol::FLARM, RadioCapability::Gfsk915));

    const auto timings = CountryRegulations::getProtocolTxTimings(CountryRegulations::Zone::ZONE1, ConspicuityProtocol::FLARM);
    REQUIRE_FALSE(timings.empty());
    tuner.protocolTxEvents.push_back({&timings.front(), 0});
    etl::string<512> status;
    etl::string_stream stream(status);
    tuner.getData(stream, "");
    REQUIRE(status.find("\"radio\":1") != status.npos);

    tx.txEnabled = false;
    REQUIRE_FALSE(tuner.selectRadio(ConspicuityProtocol::FLARM, RadioCapability::Gfsk868));
    status.clear();
    tuner.getData(stream, "");
    REQUIRE(status.find("\"radio\":null") != status.npos);
}

TEST_CASE("1090 MHz ADS-B is scheduled before GNSS on an OOK-capable receiver only")
{
    etl::message_bus<8> bus;
    MockConfig config(bus);
    GATAS::OwnshipState ownship;
    TestRadio legacy(bus, 0, RadioCapability::Gfsk868, RadioCapability::Gfsk868);
    TestRadio ook(bus, 1, RadioCapability::Ook1090, RadioCapability::Gfsk868);
    RadioTunerRx tuner(bus, config, ownship);
    tuner.radioCtxList.emplace_back(&tuner, legacy);
    tuner.radioCtxList.emplace_back(&tuner, ook);
    tuner.configuredProtocols = {{ConspicuityProtocol::ADSB, ProtocolMode::RX}};

    etl::array<uint8_t, GATAS::RADIO_PROTOCOLS.size()> boost{};
    tuner.assignProtocolsImpl(boost);
    REQUIRE(tuner.radioCtxList[0].protocolTimings.empty());
    const auto &schedule = tuner.radioCtxList[1].protocolTimings;
    REQUIRE(schedule.size() == 1);
    REQUIRE(schedule[0].protocolTimeConfig->rfConfig.baseFrequency == 1'090'000'000);
    REQUIRE(schedule[0].end() == 1000);
    REQUIRE(CountryRegulations::getProtocolTxTimings(CountryRegulations::Zone::ZONE1, ConspicuityProtocol::ADSB).empty());

    tuner.configuredProtocols.clear();
    tuner.assignProtocolsImpl(boost);
    REQUIRE(schedule.empty());
    REQUIRE(tuner.radioCtxList[1].idlePending);
}
