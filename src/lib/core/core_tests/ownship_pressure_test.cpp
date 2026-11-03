#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "binarymessages.hpp"
#include "coreutils.hpp"
#include "mockconfig.h"
#include "../../gatasconnect/ace/cobsstreamhandler.hpp"
#include "pico/time.h"

namespace
{
    class PressureReceiver : public etl::message_router<PressureReceiver, GATAS::BarometricPressureMsg, GATAS::IngressAircraftPositionsMsg>
    {
        const GATAS::OwnshipState &ownshipState;
    public:
        explicit PressureReceiver(const GATAS::OwnshipState &state) : ownshipState(state) {}
        etl::vector<GATAS::BarometricPressureSample, 8> samples;
        size_t trafficBatches = 0;
        void on_receive(const GATAS::BarometricPressureMsg &)
        {
            samples.push_back(ownshipState.loadPressureState().sample(
                GATAS::BarometricSource::External, GATAS::PressureSource::Calculated));
        }
        void on_receive(const GATAS::IngressAircraftPositionsMsg &)
        {
            ++trafficBatches;
        }
        void on_receive_unknown(const etl::imessage &) {}
    };
}

TEST_CASE("Ownship pressure V1 matches server bytes and ignores QNH", "[binarymessages][pressure]")
{
    REQUIRE(BinaryMessages::DataType::OWNSHIP_PRESSURE_V1 == 11);
    struct Case { uint8_t raw[6]; float hpa; };
    // Literal decoded fixtures from gatasServer protocol/tests/cobs_messages.rs.
    const Case cases[] = {
        {{11, 0x27, 0x95, 0x27, 0x95, 1}, 1013.3f},
        {{11, 0x23, 0x28, 0x28, 0x3C, 1}, 900.0f},
        {{11, 0, 1, 255, 255, 1}, 0.1f},
        {{11, 255, 254, 255, 255, 1}, 6553.4f},
        {{11, 255, 255, 255, 255, 1}, GATAS::INVALID_PRESSSURE_HPA},
        {{11, 255, 255, 0x27, 0x10, 1}, GATAS::INVALID_PRESSSURE_HPA},
        {{11, 0x23, 0x29, 255, 255, 1}, 900.1f},
        // Zero pressure is invalid even if a malformed sender omits the sentinel.
        {{11, 0, 0, 0x27, 0x10, 1}, GATAS::INVALID_PRESSSURE_HPA},
        // The same pressure remains usable with missing or zero QNH.
        {{11, 0x27, 0x95, 255, 255, 1}, 1013.3f},
        {{11, 0x27, 0x95, 0, 0, 1}, 1013.3f},
    };
    for (const auto &test : cases)
    {
        etl::bit_stream_reader reader(test.raw, sizeof(test.raw), etl::endian::big);
        const auto result = BinaryMessages::deserializeOwnshipPressureV1(reader);
        REQUIRE(result.has_value());
        REQUIRE(result->pressurehPa == Catch::Approx(test.hpa));
        REQUIRE(result->source == GATAS::PressureSource::Calculated);
    }
}

TEST_CASE("Ownship pressure V1 requires exactly six bytes and known sources", "[binarymessages][pressure]")
{
    uint8_t raw[] = {11, 0x23, 0x28, 0x27, 0x95, 1, 0, 0};
    for (size_t size = 0; size <= sizeof(raw); ++size)
    {
        if (size == 6)
        {
            continue;
        }
        etl::bit_stream_reader reader(raw, size, etl::endian::big);
        REQUIRE_FALSE(BinaryMessages::deserializeOwnshipPressureV1(reader));
    }
    raw[0] = 1;
    etl::bit_stream_reader wrongType(raw, 6, etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeOwnshipPressureV1(wrongType));
    raw[0] = 11;
    using Source = GATAS::PressureSource;
    const Source sources[] = {Source::Unavailable, Source::Calculated, Source::PressureSensor};
    for (uint8_t source = 0; source < 3; ++source)
    {
        raw[5] = source;
        etl::bit_stream_reader reader(raw, 6, etl::endian::big);
        const auto result = BinaryMessages::deserializeOwnshipPressureV1(reader);
        REQUIRE(result.has_value());
        REQUIRE(result->source == sources[source]);
    }
    for (const uint8_t source : {3, 4, 5, 255})
    {
        raw[5] = source;
        etl::bit_stream_reader reader(raw, 6, etl::endian::big);
        REQUIRE_FALSE(BinaryMessages::deserializeOwnshipPressureV1(reader));
    }
    const uint8_t legacy[] = {11, 0, 1, 0x8B, 0xCD, 0x27, 0x95, 1};
    etl::bit_stream_reader oldReader(legacy, sizeof(legacy), etl::endian::big);
    REQUIRE_FALSE(BinaryMessages::deserializeOwnshipPressureV1(oldReader));
}

TEST_CASE("COBS pressure updates source state before notifying and recovers after bad frames", "[binarymessages][pressure]")
{
    CoreUtils::init();
    time_us_Value = 12'345'678;
    const auto now = CoreUtils::msSinceEpoch();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    etl::message_bus<2> bus;
    MockConfig config{bus};
    PressureReceiver receiver{state};
    bus.subscribe(receiver);
    CobsStreamHandler handler{bus, state};

    // COBS encoding of [11, 0x27, 0x95, 0x27, 0x95, 1], split mid-frame.
    uint8_t first[] = {7, 11, 0x27};
    handler.handle(0.0f, 0.0f, first);
    REQUIRE(receiver.samples.empty());
    uint8_t rest[] = {0x95, 0x27, 0x95, 1, 0,
                      7, 11, 255, 255, 255, 255, 1, 0};
    handler.handle(0.0f, 0.0f, rest);
    REQUIRE(receiver.samples.size() == 2);
    REQUIRE(receiver.samples[0].value.pressurehPa == Catch::Approx(1013.3f));
    REQUIRE(receiver.samples[0].msSinceEpoch == now);
    REQUIRE(receiver.samples[0].valid);
    REQUIRE_FALSE(receiver.samples[1].valid);
    REQUIRE(receiver.samples[1].value.pressurehPa == GATAS::INVALID_PRESSSURE_HPA);
    REQUIRE_FALSE(state.calculatePressureAltitude());

    // Invalid COBS and superseded eight-byte payload, then a valid pressure frame.
    uint8_t recovery[] = {8, 11, 1, 0, 2, 11, 7, 1, 0x8B, 0xCD, 0x27, 0x95, 1, 0,
                          7, 11, 0x27, 0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, recovery);
    REQUIRE(receiver.samples.size() == 3);
    REQUIRE(receiver.samples.back().valid);
    REQUIRE(receiver.samples.back().value.pressurehPa == Catch::Approx(1013.3f));
    uint8_t unknownSource[] = {7, 11, 0x27, 0x95, 0x27, 0x95, 255, 0};
    handler.handle(0.0f, 0.0f, unknownSource);
    REQUIRE(receiver.samples.size() == 3);
    REQUIRE(state.loadPressureState().sample(GATAS::BarometricSource::External, GATAS::PressureSource::Calculated).valid);
    REQUIRE(receiver.trafficBatches == 0);
    bus.unsubscribe(receiver);
}

TEST_CASE("COBS partial frames cannot cross transports", "[binarymessages][pressure]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    etl::message_bus<2> bus;
    PressureReceiver receiver{state};
    bus.subscribe(receiver);
    CobsStreamHandler handler{bus, state};
    using Transport = GATAS::GatasConnectTransport;

    uint8_t first[] = {7, 11, 0x27};
    uint8_t rest[] = {0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, first, Transport::UDP);
    handler.handle(0.0f, 0.0f, rest, Transport::Bluetooth);
    REQUIRE(receiver.samples.empty());

    uint8_t complete[] = {7, 11, 0x27, 0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, complete, Transport::Bluetooth);
    REQUIRE(receiver.samples.size() == 1);
    REQUIRE(receiver.samples.back().value.pressurehPa == Catch::Approx(1013.3f));

    uint8_t nextFirst[] = {7, 11, 0x27};
    uint8_t nextRest[] = {0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, nextFirst, Transport::Bluetooth);
    handler.handle(0.0f, 0.0f, nextRest, Transport::UDP);
    REQUIRE(receiver.samples.size() == 1);

    uint8_t recovered[] = {7, 11, 0x27, 0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, recovered, Transport::UDP);
    REQUIRE(receiver.samples.size() == 2);
}

TEST_CASE("External COBS pressure cannot overwrite fresh internal pressure", "[binarymessages][pressure]")
{
    CoreUtils::init();
    time_us_Value = 12'345'678;
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    const auto now = CoreUtils::msSinceEpoch();
    state.updateBarometricPressure(GATAS::BarometricSource::Internal,
        {898.7628f, GATAS::PressureSource::PressureSensor}, now);
    etl::message_bus<2> bus;
    MockConfig config{bus};
    CobsStreamHandler handler{bus, state};
    // External measured pressure uses the same method key as the BMP280.
    uint8_t measured[] = {7, 11, 0x27, 0x95, 0x27, 0x95, 2, 0};
    handler.handle(0.0f, 0.0f, measured);
    uint8_t calculated[] = {7, 11, 0x27, 0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, calculated);
    const auto snapshot = state.loadPressureState();
    const auto &internal = snapshot.sample(GATAS::BarometricSource::Internal, GATAS::PressureSource::PressureSensor);
    REQUIRE(internal.value.pressurehPa == 898.7628f);
    REQUIRE(internal.msSinceEpoch == now);
    REQUIRE(snapshot.sample(GATAS::BarometricSource::External, GATAS::PressureSource::PressureSensor).valid);
    REQUIRE(snapshot.sample(GATAS::BarometricSource::External, GATAS::PressureSource::Calculated).valid);
    REQUIRE(state.calculatePressureAltitude().value() == Catch::Approx(1000.0f).margin(0.1f));
    time_us_Value += 6'000'000;
    // A new COBS update may take over when the internal reading has expired.
    uint8_t next[] = {7, 11, 0x27, 0x95, 0x27, 0x95, 1, 0};
    handler.handle(0.0f, 0.0f, next);
    REQUIRE(state.loadPressureState().sample(GATAS::BarometricSource::External, GATAS::PressureSource::Calculated).value.pressurehPa == Catch::Approx(1013.3f));
    REQUIRE(state.calculatePressureAltitude().value() == Catch::Approx(-0.416f).margin(0.1f));
}

TEST_CASE("COBS phone invalidation immediately falls back to the cached server estimate", "[binarymessages][pressure]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    etl::message_bus<2> bus;
    CobsStreamHandler handler{bus, state};

    // Phone: 1013.3 hPa measured; server: 900.0 hPa calculated.
    uint8_t phone[] = {7, 11, 0x27, 0x95, 255, 255, 2, 0};
    uint8_t server[] = {7, 11, 0x23, 0x28, 255, 255, 1, 0};
    handler.handle(0.0f, 0.0f, phone);
    handler.handle(0.0f, 0.0f, server);
    REQUIRE(state.calculatePressureAltitude().value() == Catch::Approx(-0.416f).margin(0.1f));

    uint8_t phoneUnavailable[] = {7, 11, 255, 255, 255, 255, 2, 0};
    handler.handle(0.0f, 0.0f, phoneUnavailable);
    REQUIRE(state.calculatePressureAltitude().value() == Catch::Approx(988.5f).margin(1.0f));
}
