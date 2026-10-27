
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#define private public

#include "pico/stdlib.h"

#include "../ace/gdl90service.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"
#include "ace/messagerouter.hpp"

#include "geomock.hpp"
#include "mockconfig.h"

class Test : public etl::message_router<Test, GATAS::GdlMsg>
{

public:
    GATAS::GDLData msg;
    GATAS::GDLData positionReport;
    etl::imessage_bus *bus;
    uint32_t numReceived = 0;
    Test(etl::imessage_bus *bus_) : bus(bus_)
    {
        bus->subscribe(*this);
    }
    ~Test()
    {
        bus->unsubscribe(*this);
    }

    void on_receive(const GATAS::GdlMsg &msg)
    {
        printf("GdlMsg Received %u", numReceived);
        numReceived += 1;
        this->msg = msg.msg;
        GDL90 gdl90;
        GDL90::RawBytes unpacked;
        if (gdl90.unpack(msg.msg, unpacked) && unpacked.size() == 28 &&
            (unpacked[0] == 0x0A || unpacked[0] == 0x14))
        {
            positionReport = msg.msg;
        }
    }
    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }
};

GATAS::ThreadSafeBus<50> bus;

namespace
{
    etl::optional<float> reportAltitudeFeet(const Test &receiver, uint8_t expectedId)
    {
        GDL90 gdl90;
        GDL90::RawBytes unpacked;
        REQUIRE(gdl90.unpack(receiver.positionReport, unpacked));
        REQUIRE(unpacked.size() == 28);
        REQUIRE(unpacked[0] == expectedId);
        // Read the wire field independently: 25-foot steps, offset by 1000 feet.
        const uint16_t encoded = (static_cast<uint16_t>(unpacked[11]) << 4) | (unpacked[12] >> 4);
        if (encoded == 0xFFF)
        {
            return etl::nullopt;
        }
        return static_cast<float>(encoded) * 25.0f - 1000.0f;
    }

    void setPressureAltitude(GATAS::OwnshipState &state, float altitudeMetres)
    {
        const float pressureHpa = 1013.25f * powf(1.0f - altitudeMetres / 44330.0f, 1.0f / 0.190295f);
        state.updateBarometricPressure(GATAS::BarometricSource::External,
                                      {pressureHpa, GATAS::PressureSource::Calculated});
    }
}

TEST_CASE("GDL90 callsigns are converted to uppercase", "[single-file]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service gdl90Service{bus, mockConfig, state};

    REQUIRE(gdl90Service.makeGdlCallsign("ph-Ab12") == "PH-AB12");
}

// TEST_CASE("GDL90.self_test", "[single-file]")
// {
//     GDL90 gdl90;
//     REQUIRE(gdl90.self_test());
// }

TEST_CASE("ownship position", "[single-file]")
{

    struct Case
    {
        uint32_t icao;
        float lat;
        float lon;
        int16_t alt;
        float speed;
        float vspeed;
        float track;
        const char *callSign;
        const char *name;
    };

    auto tc = GENERATE(

        Case{
            .icao = 0xFF0001,
            .lat = 53.26f,
            .lon = 4.26f,
            .alt = 6000,
            .speed = 50.0f,
            .vspeed = 2.0f,
            .track = 45.f,
            .callSign = "PH-ABC",
            .name = "53.26 4.26"},

        Case{
            .icao = 0xFF0002,
            .lat = 33.97f,
            .lon = -118.034f,
            .alt = 2000, //
            .speed = 127.0f,
            .vspeed = -4.3f,
            .track = 276.5f,
            .callSign = "",
            .name = "33.97 -118.034"},

        Case{
            .icao = 0xFF0003,
            .lat = -31.962f,
            .lon = 115.884f,
            .alt = 12000,
            .speed = 227.0f,
            .vspeed = 9.2f,
            .track = 359.5f,
            .callSign = "1234-567",
            .name = "-31.962 115.884"},

        Case{
            .icao = 0xFF0004,
            .lat = -54.8086f,
            .lon = -68.3344f,
            .alt = 000,
            .speed = 25.0f,
            .vspeed = -12.9f,
            .track = .5f,
            .callSign = "ABCDEFGH",
            .name = "-54.808 -68.334"}

    );

    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    mockConfig.ownIcao = tc.icao;
    Gdl90Service gdl90Service{bus, mockConfig, state};
    gdl90Service.postConstruct();
    Test test{&bus};

    INFO(tc.name);

    GATAS::OwnshipPositionInfo thisIsUs{
        .timestamp = 1700000000, // Unix timestamp
        .lat = tc.lat,           // Amsterdam area
        .lon = tc.lon,
        .ellipseHeight = tc.alt,    // ~1000m MSL + ~47m geoid separation for NL
        .verticalSpeed = tc.vspeed, // Stable cruise
        .groundSpeed = tc.speed,    // ~107 knots, typical light aircraft cruise
        .track = tc.track,          // Heading west
        .hTurnRate = 0.0f,          // Straight and level
//        .velocityNorth = 0.0f,      // Pure westward track
//        .velocityEast = -55.0f,     // Matches groundSpeed westward
        .geoidSeparation = 47,      // ~47m for the Netherlands (NL geoid is above WGS84)
        .airborne = true,
        .conspicuity = GATAS::Config::Conspicuity{},
    };

    state.location.store(thisIsUs);
    state.updateBarometricPressure(GATAS::BarometricSource::External,
        {1013.25f * powf(1.0f - static_cast<float>(thisIsUs.heightMsl()) / 44330.0f, 1.0f / 0.190295f),
         GATAS::PressureSource::Calculated});
    GATAS::OwnshipPositionMsg msg{};

    gdl90Service.on_receive(msg);

    // Receive the message back
    GDL90 gdl90;
    etl::vector<uint8_t, 64> unpacked;
    gdl90.unpack(test.msg, unpacked);

    bool is_ownership = true;
    GDL90::ALERT_STATUS alert_status = GDL90::ALERT_STATUS::__LAST;
    GDL90::ADDR_TYPE addr_type = GDL90::ADDR_TYPE::__LAST;
    uint32_t participant_address = 0xffaa55;
    float latitude_f = -179.2255;
    float longitude_f = +179.4357;
    float altitude_f = 101349;
    uint32_t misc = GDL90::MISC_ALLOWED_MASK;
    GDL90::NIC nic = GDL90::NIC::__LAST;
    GDL90::NACP nacp = GDL90::NACP::__LAST;
    float horiz_velocity_f = 125.4462;
    float vert_velocity_f = -800.333;
    float track_hdg_f = 358.3674;
    GDL90::EMITTER emitter = GDL90::EMITTER::__LAST;
    etl::string<8> call_sign = "";
    GDL90::EMERGENCY_PRIO emergency_prio_code = GDL90::EMERGENCY_PRIO::__LAST;
    uint32_t latitude;
    uint32_t longitude;
    uint32_t altitude;
    uint32_t horiz_velocity;
    uint32_t vert_velocity;
    uint32_t track_hdg;

    REQUIRE(gdl90.ownership_or_traffic_report_decode(unpacked, is_ownership, alert_status, addr_type, participant_address,
                                                     latitude, longitude, altitude, misc, nic, nacp, horiz_velocity, vert_velocity, track_hdg,
                                                     emitter, call_sign, emergency_prio_code));
    REQUIRE(gdl90.latlon_decode(latitude, latitude_f));
    REQUIRE(gdl90.latlon_decode(longitude, longitude_f));
    REQUIRE(gdl90.altitude_decode(altitude, altitude_f));
    REQUIRE(gdl90.horizontal_velocity_decode(horiz_velocity, horiz_velocity_f));
    REQUIRE(gdl90.vertical_velocity_decode(vert_velocity, vert_velocity_f));
    REQUIRE(gdl90.track_hdg_decode(track_hdg, track_hdg_f));

    REQUIRE(test.numReceived == 1);
    REQUIRE(test.msg.size() == 32);
    REQUIRE(latitude_f == Catch::Approx(tc.lat).margin(0.001));
    REQUIRE(longitude_f == Catch::Approx(tc.lon).margin(0.001));
    REQUIRE(altitude_f == Catch::Approx(thisIsUs.heightMsl() * M_TO_FT).margin(25.f));
    REQUIRE(track_hdg_f == Catch::Approx(tc.track).margin(1));
    REQUIRE((vert_velocity_f * FTPMIN_TO_MS) == Catch::Approx(tc.vspeed).margin(0.3));
    REQUIRE((horiz_velocity_f * KN_TO_MS) == Catch::Approx(tc.speed).margin(1));
    REQUIRE(call_sign.length() == etl::string_view(tc.callSign).length());
    REQUIRE(call_sign == etl::string_view(tc.callSign));
}

TEST_CASE("heartbeat uses GDL90 wire byte order", "[single-file]")
{
    GDL90 gdl90;
    const bool hasFix = GENERATE(false, true);
    const bool timestampHighBit = GENERATE(false, true);
    const uint32_t status = GDL90::HEARTBEAT_STATUS_UAT_INITIALIZED_MASK |
                            (hasFix ? GDL90::HEARTBEAT_STATUS_GPS_POS_VALID_MASK : 0);
    const uint32_t timestamp = timestampHighBit ? 0x11234 : 0x01234;

    // ICD section 3.1: status byte 1 first, timestamp low byte first.
    // Nonzero message counts also ensure their byte order stays unchanged.
    const GDL90::RawBytes expected{
        0x00, uint8_t(hasFix ? 0x81 : 0x01), uint8_t(timestampHighBit ? 0x80 : 0x00),
        0x34, 0x12, 0x22, 0x37};
    GDL90::RawBytes unpacked;
    REQUIRE(gdl90.heartbeat_encode(unpacked, status, timestamp, 4, 567));
    REQUIRE(unpacked == expected);

    uint32_t decodedStatus = 0;
    uint32_t decodedTimestamp = 0;
    uint32_t uplinkCount = 0;
    uint32_t trafficCount = 0;
    REQUIRE(gdl90.heartbeat_decode(expected, decodedStatus, decodedTimestamp, uplinkCount, trafficCount));
    REQUIRE(decodedStatus == status);
    REQUIRE(decodedTimestamp == timestamp);
    REQUIRE(uplinkCount == 4);
    REQUIRE(trafficCount == 567);
}

TEST_CASE("heartbeat", "[single-file]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};

    // CircularPosition center = CircularPosition(20'000, 15, {52.f, 0.f, 0});
    // auto mainPos = center.take();

    Gdl90Service gdl90Service{bus, mockConfig, state};
    gdl90Service.postConstruct();
    Test test{&bus};

    gdl90Service.sendHeartBeat(gdl90Service);

    // print_buffer_hex<uint8_t>(test.msg);
    REQUIRE(test.numReceived == 2);
    REQUIRE(test.msg.size() == 43);
    GDL90 gdl90;
    GDL90::RawBytes unpacked;
    REQUIRE(gdl90.unpack(test.msg, unpacked));
    uint64_t serial = 0;
    uint32_t capabilities = 0;
    etl::string<8> name;
    etl::string<16> longName;
    REQUIRE(gdl90.foreflight_id_decode(unpacked, serial, name, longName, capabilities));
    // Capability bit 0 is clear: altitude is WGS-84 ellipsoid altitude.
    REQUIRE(capabilities == GDL90::FOREFLIGHT_CAPABILITIES_INTERNET_UNRESTRICTED);
}

TEST_CASE("GDL90 service geometric altitude uses ellipsoid height", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    Test receiver{&bus};
    GATAS::GpsStats stats;
    stats.gpsFix = GATAS::GpsFix{GATAS::GpsFixType::D3};
    service.on_receive(GATAS::GpsStatsMsg{stats});
    GATAS::OwnshipPositionInfo position{};
    position.ellipseHeight = GENERATE(100, 20);
    position.geoidSeparation = 47;
    state.location.store(position);
    service.on_receive(GATAS::OwnshipPositionMsg{});

    GDL90 gdl90;
    GDL90::RawBytes unpacked;
    REQUIRE(gdl90.unpack(receiver.msg, unpacked));
    uint32_t altitude = 0;
    uint32_t merit = 0;
    bool warning = false;
    REQUIRE(gdl90.ownership_geometric_altitude_decode(unpacked, altitude, warning, merit));
    float feet = 0.f;
    REQUIRE(gdl90.geo_altitude_decode(altitude, feet));
    REQUIRE(feet == Catch::Approx(position.ellipseHeight * M_TO_FT).margin(5.f));
}

TEST_CASE("GDL90 sends egress pressure regardless of ownship pressure and MSL fallback", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    service.mslAltFallback = GENERATE(false, true);
    Test receiver{&bus};
    GDL90 gdl90;

    const bool ownshipHasPressure = GENERATE(false, true);
    struct Case
    {
        int32_t meters;
        uint8_t highByte;
        uint8_t lowNibble;
    };
    // Independent ICD values: feet are offset by 1000 and quantized in 25-foot steps.
    // Repeated reports exercise state updates and invalidation after a valid sample.
    const Case cases[] = {
        {500, 0x06, 0x90},
        {1000, 0x0A, 0xB0},
        {0, 0x02, 0x80},
        {-100, 0x01, 0xA0},
        {-400, 0xFF, 0xF0},
        {31000, 0xFF, 0xF0},
    };
    for (const auto &test : cases)
    {
        INFO(test.meters);
        // Native traffic pressure wins over conflicting geometric height and ownship pressure.
        if (ownshipHasPressure)
        {
            setPressureAltitude(state, 900.0f);
        }
        const auto previousCount = receiver.numReceived;
        GATAS::OwnshipPositionInfo ownPosition{};
        ownPosition.ellipseHeight = 1047;
        ownPosition.geoidSeparation = 47;
        ownPosition.baroAlt = 7000;
        state.location.store(ownPosition);
        GATAS::AircraftPositionInfo position{};
        position.dataSource = GATAS::DataSource::ADSB;
        position.ellipseHeight = 1047;
        position.pressureAlt = test.meters;
        service.on_receive(GATAS::EgressAircraftPositionMsg{position});
        REQUIRE(receiver.numReceived == previousCount + 1);
        GDL90::RawBytes unpacked;
        REQUIRE(gdl90.unpack(receiver.msg, unpacked));
        REQUIRE(unpacked.size() == 28);
        REQUIRE(unpacked[0] == 0x14);
        REQUIRE(unpacked[11] == test.highByte);
        REQUIRE((unpacked[12] & 0xF0) == test.lowNibble);
    }
}

TEST_CASE("GDL90 ownship altitude policy is strict by default", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    Test receiver{&bus};
    GATAS::OwnshipPositionInfo position{};
    position.timestamp = 1;
    position.ellipseHeight = 1600;
    state.location.store(position);

    GDL90 gdl90;
    GDL90::RawBytes unpacked;
    uint32_t latitude = 0;
    uint32_t longitude = 0;
    uint32_t altitude = 0;
    float feet = 0.0f;
    bool isOwnership = true;
    GDL90::ALERT_STATUS alertStatus = GDL90::ALERT_STATUS::__LAST;
    GDL90::ADDR_TYPE addressType = GDL90::ADDR_TYPE::__LAST;
    uint32_t address = 0;
    uint32_t misc = 0;
    GDL90::NIC nic = GDL90::NIC::__LAST;
    GDL90::NACP nacp = GDL90::NACP::__LAST;
    uint32_t horizontalVelocity = 0;
    uint32_t verticalVelocity = 0;
    uint32_t track = 0;
    GDL90::EMITTER emitter = GDL90::EMITTER::__LAST;
    etl::string<8> callSign;
    GDL90::EMERGENCY_PRIO emergencyPriority = GDL90::EMERGENCY_PRIO::__LAST;

    service.on_receive(GATAS::OwnshipPositionMsg{});
    REQUIRE(gdl90.unpack(receiver.msg, unpacked));
    REQUIRE(gdl90.ownership_or_traffic_report_decode(
        unpacked, isOwnership, alertStatus, addressType, address, latitude, longitude, altitude, misc, nic, nacp,
        horizontalVelocity, verticalVelocity, track, emitter, callSign, emergencyPriority));
    REQUIRE(gdl90.altitude_decode(altitude, feet));
    REQUIRE(std::isnan(feet));

    service.mslAltFallback = true;
    service.on_receive(GATAS::OwnshipPositionMsg{});
    REQUIRE(gdl90.unpack(receiver.positionReport, unpacked));
    REQUIRE(gdl90.ownership_or_traffic_report_decode(
        unpacked, isOwnership, alertStatus, addressType, address, latitude, longitude, altitude, misc, nic, nacp,
        horizontalVelocity, verticalVelocity, track, emitter, callSign, emergencyPriority));
    REQUIRE(gdl90.altitude_decode(altitude, feet));
    REQUIRE(feet == Catch::Approx(1600.0f * M_TO_FT).margin(25.0f));
}

TEST_CASE("GDL90 traffic altitude selection follows pressure availability and MSL fallback", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    service.mslAltFallback = GENERATE(false, true);
    const bool ownshipHasPressure = GENERATE(false, true);
    const bool trafficHasPressure = GENERATE(false, true);
    Test receiver{&bus};

    GATAS::OwnshipPositionInfo ownship{};
    ownship.ellipseHeight = 1047;
    ownship.geoidSeparation = 47;
    state.location.store(ownship);
    if (ownshipHasPressure)
    {
        setPressureAltitude(state, 900.0f);
    }
    GATAS::AircraftPositionInfo target{};
    target.ellipseHeight = 1147;
    target.pressureAlt = trafficHasPressure ? 500 : GATAS::INVALID_BARO_ALTITUDE;

    service.on_receive(GATAS::OwnshipPositionMsg{});
    const auto ownAltitude = reportAltitudeFeet(receiver, 0x0A);
    if (ownshipHasPressure || service.mslAltFallback)
    {
        const float expectedOwnMetres = ownshipHasPressure ? 900.0f : 1000.0f;
        REQUIRE(ownAltitude.has_value());
        REQUIRE(ownAltitude.value() == Catch::Approx(expectedOwnMetres * M_TO_FT).margin(25.0f));
    }
    else
    {
        REQUIRE_FALSE(ownAltitude.has_value());
    }

    service.on_receive(GATAS::EgressAircraftPositionMsg{target});
    const auto altitude = reportAltitudeFeet(receiver, 0x14);
    if (trafficHasPressure || ownshipHasPressure || service.mslAltFallback)
    {
        // Native pressure wins; otherwise map the +100 m difference onto ownship pressure,
        // or use target MSL only when neither pressure altitude is available and fallback is enabled.
        const float expectedMetres = trafficHasPressure ? 500.0f : (ownshipHasPressure ? 1000.0f : 1100.0f);
        REQUIRE(altitude.has_value());
        REQUIRE(altitude.value() == Catch::Approx(expectedMetres * M_TO_FT).margin(25.0f));
    }
    else
    {
        REQUIRE_FALSE(altitude.has_value());
    }
    REQUIRE(target.pressureAlt == (trafficHasPressure ? 500 : GATAS::INVALID_BARO_ALTITUDE));
}

TEST_CASE("GDL90 maps ellipsoid separation onto ownship pressure with either fallback setting", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    service.mslAltFallback = GENERATE(false, true);
    Test receiver{&bus};
    GATAS::OwnshipPositionInfo ownship{};
    ownship.ellipseHeight = 1047;
    ownship.geoidSeparation = 47;
    state.location.store(ownship);
    GATAS::AircraftPositionInfo target{};

    for (const float anchor : {900.0f, 1200.0f})
    {
        setPressureAltitude(state, anchor);
        service.on_receive(GATAS::OwnshipPositionMsg{});
        const auto ownAltitude = reportAltitudeFeet(receiver, 0x0A);
        REQUIRE(ownAltitude.has_value());
        for (const int32_t separation : {100, -100, 0})
        {
            target.ellipseHeight = ownship.ellipseHeight + separation;
            service.on_receive(GATAS::EgressAircraftPositionMsg{target});
            const auto altitude = reportAltitudeFeet(receiver, 0x14);
            REQUIRE(altitude.has_value());
            REQUIRE(altitude.value() == Catch::Approx((anchor + separation) * M_TO_FT).margin(25.0f));
            REQUIRE(altitude.value() - ownAltitude.value() ==
                    Catch::Approx(static_cast<float>(separation) * M_TO_FT).margin(25.0f));
        }
    }
}

TEST_CASE("GDL90 altitude mapping accepts sea-level and negative heights", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    service.mslAltFallback = GENERATE(false, true);
    Test receiver{&bus};
    GATAS::OwnshipPositionInfo ownship{};
    ownship.ellipseHeight = 0;
    state.location.store(ownship);
    setPressureAltitude(state, -50.0f);
    GATAS::AircraftPositionInfo target{};
    target.ellipseHeight = GENERATE(-50, 0, 100);
    service.on_receive(GATAS::EgressAircraftPositionMsg{target});
    const auto altitude = reportAltitudeFeet(receiver, 0x14);
    REQUIRE(altitude.has_value());
    REQUIRE(altitude.value() == Catch::Approx((-50.0f + target.ellipseHeight) * M_TO_FT).margin(25.0f));
}

TEST_CASE("GDL90 altitude encoder rejects missing and out-of-range values", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    REQUIRE(service.encodeAltitude(etl::nullopt) == 0xFFF);
    REQUIRE(service.encodeAltitude(-305) == 0xFFF);
    REQUIRE(service.encodeAltitude(-304) == 0);
    REQUIRE(service.encodeAltitude(30'891) != 0xFFF);
    REQUIRE(service.encodeAltitude(30'892) == 0xFFF);
}

TEST_CASE("Traffic altitude uses whole metre ownship pressure", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    GATAS::OwnshipPositionInfo ownship{};
    ownship.ellipseHeight = 1047;
    state.location.store(ownship);
    GATAS::AircraftPositionInfo target{};
    target.ellipseHeight = 1147;

    const float anchor = GENERATE(900.6f, -50.6f);
    setPressureAltitude(state, anchor);
    REQUIRE(CoreUtils::resolveTrafficAltitude(target, state, false).value() == static_cast<int32_t>(anchor) + 100);
}

TEST_CASE("Derived traffic altitude does not overflow integer metres", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    GATAS::OwnshipPositionInfo ownship{};
    GATAS::AircraftPositionInfo target{};
    ownship.ellipseHeight = GENERATE(INT32_MIN, INT32_MAX);
    target.ellipseHeight = ownship.ellipseHeight == INT32_MIN ? INT32_MAX : INT32_MIN;
    state.location.store(ownship);
    setPressureAltitude(state, 900.0f);
    REQUIRE_FALSE(CoreUtils::resolveTrafficAltitude(target, state, false).has_value());
}

TEST_CASE("GDL90 unavailable values and signed encodings", "[gdl90]")
{
    GDL90 g;
    float value = 0.f;
    uint32_t raw = 0;
    bool magnetic = false;
    REQUIRE(g.horizontal_velocity_decode(0xfff, value));
    REQUIRE(std::isnan(value));
    REQUIRE(g.horizontal_velocity_decode(0x800, value));
    REQUIRE(value == 2048.f);
    REQUIRE_FALSE(g.horizontal_velocity_decode(0x1000, value));
    REQUIRE(g.height_decode(0x8000, value));
    REQUIRE(std::isnan(value));
    REQUIRE(g.height_encode(raw, std::nanf("")));
    REQUIRE(raw == 0x8000);
    GDL90::RawBytes bytes;
    REQUIRE(g.height_above_terrain_encode(bytes, raw));
    REQUIRE(bytes == GDL90::RawBytes{9, 0x80, 0});
    GDL90::MESSAGE_ID id;
    REQUIRE(g.id_decode(id, bytes));
    REQUIRE(id == GDL90::MESSAGE_ID::HEIGHT_ABOVE_TERRAIN);
    REQUIRE(g.foreflight_heading_decode(0xffff, value, magnetic));
    REQUIRE(std::isnan(value));
    REQUIRE(g.geo_altitude_encode(raw, -1000.f));
    REQUIRE(raw == 0xff38);
    REQUIRE(g.geo_altitude_decode(raw, value));
    REQUIRE(value == -1000.f);
    REQUIRE_FALSE(g.geo_altitude_encode(raw, std::nanf("")));
    REQUIRE(g.foreflight_roll_pitch_encode(raw, -10.f));
    REQUIRE(raw == 0xff9c);
    REQUIRE(g.foreflight_roll_pitch_decode(raw, value));
    REQUIRE(value == -10.f);
    REQUIRE(g.foreflight_heading_encode(raw, -10.f, false));
    REQUIRE(raw == 0x7f9c);
    REQUIRE(g.foreflight_heading_decode(raw, value, magnetic));
    REQUIRE(value == -10.f);
    REQUIRE_FALSE(magnetic);
    REQUIRE(uint32_t(GDL90::EMITTER::GLIDER_SAILPLANE) == 9);
    REQUIRE(uint32_t(GDL90::NACP::LT_0_05_NM) == 8);
}

TEST_CASE("GDL90 initialization uses specification byte order", "[gdl90]")
{
    GDL90 g;
    GDL90::RawBytes bytes;
    const GDL90::RawBytes expected{2, 0x43, 0x02};
    const uint32_t config = GDL90::INIT_CONFIG_CDTI_OK_MASK | GDL90::INIT_CONFIG_AUDIO_INHIBIT_MASK |
                            GDL90::INIT_CONFIG_AUDIO_TEST_MASK | GDL90::INIT_CONFIG_CSA_AUDIO_DISABLE_MASK;
    REQUIRE(g.initialization_encode(bytes, config));
    REQUIRE(bytes == expected);
    uint32_t decoded = 0;
    REQUIRE(g.initialization_decode(expected, decoded));
    REQUIRE(decoded == config);
}

TEST_CASE("GDL90 UAT timestamps use least significant byte first", "[gdl90]")
{
    GDL90 g;
    const auto kind = GENERATE(7, 30, 31);
    const size_t payloadSize = kind == 7 ? 432 : (kind == 30 ? 18 : 34);
    etl::vector<uint8_t, 432> payload;
    payload.resize(payloadSize, 0x7e);
    etl::vector<uint8_t, 436> bytes;
    auto encode = kind == 7 ? &GDL90::uplink_data_encode : (kind == 30 ? &GDL90::basic_uat_report_encode : &GDL90::long_uat_report_encode);
    auto decode = kind == 7 ? &GDL90::uplink_data_decode : (kind == 30 ? &GDL90::basic_uat_report_decode : &GDL90::long_uat_report_decode);
    REQUIRE((g.*encode)(bytes, 1000000, payload));
    REQUIRE(bytes.size() == payloadSize + 4);
    REQUIRE(bytes[0] == kind);
    REQUIRE(bytes[1] == 0x40);
    REQUIRE(bytes[2] == 0x42);
    REQUIRE(bytes[3] == 0x0f);
    etl::vector<uint8_t, 432> decoded;
    uint32_t time = 0;
    REQUIRE((g.*decode)(bytes, time, decoded));
    REQUIRE(time == 1000000);
    REQUIRE(decoded == payload);
#ifndef NDEBUG
    etl::vector<uint8_t, 1> small;
    REQUIRE_FALSE((g.*encode)(small, 1000000, payload));
    REQUIRE_FALSE((g.*decode)(bytes, time, small));
#endif
    etl::vector<uint8_t, 878> framed;
    REQUIRE(g.pack(framed, bytes));
    etl::vector<uint8_t, 436> restored;
    REQUIRE(g.unpack(framed, restored));
    REQUIRE(restored == bytes);
#ifndef NDEBUG
    if (kind == 7)
    {
        GDL90::RawBytes reportBuffer;
        REQUIRE_FALSE((g.*encode)(reportBuffer, 1000000, payload));
    }
#endif
}

TEST_CASE("ForeFlight ID supports all documented internet policies", "[gdl90]")
{
    GDL90 g;
    const uint32_t policy = GENERATE(0u, 2u, 4u);
    const uint32_t datum = GENERATE(0u, 1u);
    GDL90::RawBytes expected;
    expected.resize(39, 0);
    expected[0] = 0x65;
    expected[2] = 1;
    expected[38] = policy | datum;
    uint64_t serial = 0;
    uint32_t capabilities = 0;
    etl::string<8> name;
    etl::string<16> longName;
    REQUIRE(g.foreflight_id_decode(expected, serial, name, longName, capabilities));
    REQUIRE(capabilities == (policy | datum));
    GDL90::RawBytes bytes;
    REQUIRE(g.foreflight_id_encode(bytes, 0, "", "", policy | datum));
    REQUIRE(bytes[38] == (policy | datum));
    REQUIRE_FALSE(g.foreflight_id_encode(bytes, 0, "", "", 8));
}

TEST_CASE("GDL90 framing validates CRC escapes and capacity", "[gdl90]")
{
    GDL90 g;
    // ICD section 2.2.4, independently specified CRC and heartbeat payload.
    const GDL90::RawBytes golden{0x7e, 0, 0x81, 0x41, 0xdb, 0xd0, 8, 2, 0xb3, 0x8b, 0x7e};
    etl::vector<uint8_t, 7> decoded;
    REQUIRE(g.unpack(golden, decoded));
    REQUIRE(decoded == GDL90::RawBytes{0, 0x81, 0x41, 0xdb, 0xd0, 8, 2});
    GDL90::RawBytes framed;
    REQUIRE(g.pack(framed, decoded));
    REQUIRE(framed == golden);
#ifndef NDEBUG
    etl::vector<uint8_t, 6> small;
    REQUIRE_FALSE(g.unpack(golden, small));
    REQUIRE_FALSE(g.pack(small, decoded));
#endif
    auto corrupted = golden;
    corrupted[5] ^= 1;
    REQUIRE_FALSE(g.unpack(corrupted, decoded));
    const auto malformed = GENERATE(
        GDL90::RawBytes{0x7e, 0, 0, 0x7e},
        GDL90::RawBytes{0x7e, 0x7d, 0x20, 0x81, 0x41, 0xdb, 0xd0, 8, 2, 0xb3, 0x8b, 0x7e},
        GDL90::RawBytes{0x7e, 0, 0x7e, 0, 0, 0x7e},
        GDL90::RawBytes{0x7e, 0, 0, 0x7d, 0x7e});
    REQUIRE_FALSE(g.unpack(malformed, decoded));
    GDL90::RawBytes escaped{0, 0x7d, 0x7e};
    REQUIRE(g.pack(framed, escaped));
    REQUIRE(g.unpack(framed, decoded));
    REQUIRE(decoded == escaped);
#ifndef NDEBUG
    escaped.resize(28, 0x7e);
    REQUIRE_FALSE(g.pack(framed, escaped));
#endif
}

#ifndef NDEBUG
TEST_CASE("GDL90 service does not publish failed packets", "[gdl90]")
{
    CoreUtils::init();
    GATAS::OwnshipState state;
    state.init(CoreUtils::sharedSpinLock());
    MockConfig mockConfig{bus};
    Gdl90Service service{bus, mockConfig, state};
    Test receiver{&bus};
    GDL90::RawBytes bytes{0};
    bytes.resize(28, 0x7e);
    service.packAndSend(bytes);
    REQUIRE(receiver.numReceived == 0);
    REQUIRE(service.statistics.packingFailureErr == 1);
}
#endif
