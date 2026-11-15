#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "ace/adslace.hpp"
#include "ace/messagerouter.hpp"
#include "mockconfig.h"

namespace
{
    class UplinkEncoder : public ADSL::Connector
    {
        etl::array<uint32_t, 64> scratch{};

    public:
        etl::array<uint8_t, 256> frame{};
        size_t length = 0;

        uint32_t adsl_getTick() const override { return CoreUtils::timeUs32(); }
        uint8_t *adsl_alloc(const void *, size_t size) override
        {
            REQUIRE(size <= sizeof(scratch));
            return reinterpret_cast<uint8_t *>(scratch.data());
        }
        bool adsl_sendFrame(const void *, const uint8_t *data, size_t size) override
        {
            REQUIRE(size <= frame.size());
            std::memcpy(frame.data(), data, size);
            length = size;
            return true;
        }
        void adsl_receivedTraffic(const ADSL::Header &, const ADSL::TrafficPayload &) override {}
        void adsl_receivedStatus(const ADSL::Header &, const ADSL::StatusPayload &) override {}
        void adsl_receivedUplinkTraffic(const ADSL::Header &, etl::span<const ADSL::UplinkEntry>) override {}
        void adsl_buildStatusPayload(const void *, ADSL::StatusPayload &) override {}
    };

    class PositionCollector : public etl::message_router<PositionCollector, GATAS::IngressAircraftPositionsMsg>
    {
    public:
        size_t batches = 0;
        etl::vector<GATAS::AircraftPositionInfo, GATAS::IngressAircraftPositionsMsg::MAX_POSITIONS> positions;

        void on_receive(const GATAS::IngressAircraftPositionsMsg &msg)
        {
            ++batches;
            positions = msg.positions;
        }
        void on_receive_unknown(const etl::imessage &) {}
    };

    struct UplinkFixture
    {
        GATAS::ThreadSafeBus<8> bus;
        MockConfig config{bus};
        GATAS::OwnshipState ownshipState;
        ADSLAce receiver{bus, config, ownshipState};
        PositionCollector collector;

        UplinkFixture()
        {
            xSemaphoreTakeValue = pdTRUE;
            CoreUtils::init();
            ownshipState.init(CoreUtils::sharedSpinLock());
            GATAS::OwnshipPositionInfo ownship{};
            ownship.lat = 52.0f;
            ownship.lon = 4.0f;
            ownship.conspicuity.icaoAddress = 0xACEACE;
            ownshipState.location.store(ownship);
            time_us_Value = 25'000'000;
            CoreUtils::setOffsetMsSinceEpoch(1'700'000'040'000ULL);
            REQUIRE(receiver.postConstruct() == GATAS::PostConstruct::OK);
            receiver.start();
            bus.subscribe(collector);
        }

        void receive(etl::span<const ADSL::UplinkEntry> entries)
        {
            UplinkEncoder encoder;
            ADSL::Protocol transmitter{&encoder};
            transmitter.init();
            transmitter.setAddress(0x123456);
            transmitter.rqSendUplinkPayload(nullptr, false, entries);
            REQUIRE(encoder.length != 0);
            REQUIRE(encoder.length % sizeof(uint32_t) == 0);
            auto &pool = BaseModule::getGlobalPool();
            auto *data = static_cast<uint8_t *>(pool.alloc(encoder.length));
            REQUIRE(data != nullptr);
            std::memcpy(data, encoder.frame.data(), encoder.length);
            bus.receive(GATAS::RadioRxMsg{pool, data, encoder.length,
                0, 869'525'000, GATAS::DataSource::ADSLO_HDR, -42});
        }
    };

    ADSL::UplinkEntry makeEntry(uint32_t address, size_t index)
    {
        ADSL::TrafficPayload payload;
        payload.timestamp(CoreUtils::msSinceEpoch() - 1'000);
        payload.latitude(52.0f + static_cast<float>(index) * 0.01f);
        payload.longitude(4.0f + static_cast<float>(index) * 0.01f);
        payload.altitude(1'000 + static_cast<int32_t>(index) * 100);
        payload.speed(30.0f + static_cast<float>(index));
        payload.groundTrack(90.0f);
        payload.verticalRate(1.0f);
        payload.aircraftCategory(ADSL::TrafficPayload::AircraftCategory::LIGHT_FIXED_WING);
        payload.flightState(ADSL::TrafficPayload::FlightState::AIRBORNE);
        return ADSL::UplinkEntry{0x05, address, payload};
    }
}

TEST_CASE("Full ADS-L uplink packets retain all ten targets in one owned batch", "[adsl][uplink]")
{
    UplinkFixture fixture;
    etl::array<ADSL::UplinkEntry, ADSL::Protocol::MAX_UPLINK_TARGETS> entries;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        entries[i] = makeEntry(0x100000 + static_cast<uint32_t>(i), i);
    }

    // Repeated full packets also exercise reuse of the receive pool allocation.
    for (size_t packet = 0; packet < 4; ++packet)
    {
        fixture.receive(entries);
        REQUIRE(fixture.collector.batches == packet + 1);
        REQUIRE(fixture.collector.positions.size() == entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto &position = fixture.collector.positions[i];
            const auto &payload = entries[i].payload();
            CHECK(position.address == entries[i].address().asUint());
            CHECK(position.dataSource == GATAS::DataSource::ADSLO_HDR);
            CHECK(position.addressType == GATAS::AddressType::ICAO);
            CHECK(position.airborne);
            CHECK(position.lat == Catch::Approx(payload.latitude()).margin(0.0001f));
            CHECK(position.lon == Catch::Approx(payload.longitude()).margin(0.0001f));
            CHECK(position.ellipseHeight == payload.altitude());
            const auto ageMs = CoreUtils::msSinceEpoch() - payload.timestampRestored(CoreUtils::msSinceEpoch());
            CHECK(position.timestamp == CoreUtils::timeUs32() - static_cast<uint32_t>(ageMs * 1'000));
        }
    }
}

TEST_CASE("ADS-L uplink filtering preserves the remaining batch and suppresses ownship-only output", "[adsl][uplink]")
{
    UplinkFixture fixture;
    etl::array<ADSL::UplinkEntry, ADSL::Protocol::MAX_UPLINK_TARGETS> entries;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        entries[i] = makeEntry(0x200000 + static_cast<uint32_t>(i), i);
    }
    entries[4] = makeEntry(0xACEACE, 4);
    fixture.receive(entries);
    REQUIRE(fixture.collector.batches == 1);
    REQUIRE(fixture.collector.positions.size() == entries.size() - 1);
    for (size_t i = 0; i < fixture.collector.positions.size(); ++i)
    {
        CHECK(fixture.collector.positions[i].address == entries[i < 4 ? i : i + 1].address().asUint());
    }

    const etl::array<ADSL::UplinkEntry, 1> ownshipOnly{makeEntry(0xACEACE, 0)};
    fixture.receive(ownshipOnly);
    REQUIRE(fixture.collector.batches == 1);
}
