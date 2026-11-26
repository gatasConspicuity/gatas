
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "../include/adsl/protocol.hpp"
#include "etl/vector.h"
#include "helpers.hpp"
#include <vector>

using namespace ADSL;

// // Test connector that captures objects passed via the Connector interface
// 56 words = 224 bytes — large enough for the maximum uplink frame
// (N=10 entries @ 19 bytes each: 1 network + 196 padded + 3 CRC = 200 bytes = 50 words)
etl::array<uint32_t, 56> localMemory{};
etl::span<uint32_t> bufferSpan(localMemory.data(), localMemory.size());

etl::array<uint32_t, 56> newMemory{};
etl::span<uint32_t> newBufferSpan(localMemory.data(), localMemory.size());
struct TestConnector : public Connector
{
    uint32_t tick = 1000;
    FrameBuffer lastFrame{bufferSpan};
    TrafficPayload lastTrafficPayload{};
    bool trafficSet = false;
    StatusPayload lastStatusPayload{};
    bool statusSet = false;
    Header lastHeader{};
    bool headerSet = false;
    const void *lastCtx;

    uint32_t adsl_getTick() const override { return tick; }

    virtual bool adsl_sendFrame(const void *ctx, uint8_t const *data, size_t lengthBytes) override
    {
        lastFrame.assign(etl::span<const uint8_t>(data, lengthBytes));
        lastFrame.used_bytes = lengthBytes;
        lastCtx = ctx;
        return true;
    }

    virtual void adsl_receivedTraffic(const Header &header, const TrafficPayload &tp) override
    {
        lastTrafficPayload = tp;
        trafficSet = true;
        lastHeader = header;
        headerSet = true;
    }

    virtual void adsl_receivedStatus(const ADSL::Header &header, const StatusPayload &sp) override
    {
        lastStatusPayload = sp;
        statusSet = true;
        lastHeader = header;
        headerSet = true;
    }

    int uplinkCount = 0;
    UplinkEntry lastUplinkEntry{};

    virtual void adsl_receivedUplinkTraffic(const Header &header, etl::span<const UplinkEntry> entries) override
    {
        for (const auto &entry : entries)
        {
            lastUplinkEntry = entry;
            uplinkCount++;
        }
        lastHeader = header;
    }

    virtual void adsl_buildStatusPayload(const void *ctx, StatusPayload &sp) override
    {
        (void)ctx;
        sp.mBandReceiveCapability(StatusPayload::ReceiveCapability::Partial);
        sp.oBandHdrReceiveCapability(StatusPayload::ReceiveCapability::Partial);
    }

    virtual uint8_t *adsl_alloc(const void *ctx, size_t sizeBytes) override
    {
        static uint8_t memoryPool[256];
        return memoryPool;
    }
};
const auto buffer = etl::make_array<uint32_t>(0xB229CC00, 0x9981C4E4, 0x5DD2E995, 0x20492D0B, 0xBD66A043, 0xEE82CD94);

// ============================================================================
// Default Constructor Tests
// ============================================================================

TEST_CASE("Protocol handleRx", "[Protocol]")
{
    TestConnector conn;
    Protocol protocol(&conn);
    protocol.crcCheckOnReceive = true;

    REQUIRE(protocol.handleRx(-42, etl::span<uint32_t>{}) == Protocol::RxStatudeCode::UNEXPECTED_BUFFER_SIZE);

    // Actual generated and validated ADSL packet
    auto newBuffer = buffer;
    FrameBuffer buf{newBuffer.data(), newBuffer.size()};
    protocol.crcCheckOnReceive = true;
    auto result = protocol.handleRx(-42, buf.fullSpan32());

    REQUIRE(result == Protocol::RxStatudeCode::OK);

    // Validate header fields
    REQUIRE(conn.headerSet);
    REQUIRE(conn.lastHeader.type() == Header::PayloadTypeIdentifier::TRAFFIC);
    REQUIRE(conn.lastHeader.addressMappingTable() == 0x05);
    REQUIRE(conn.lastHeader.address().asUint() == 0xD91A6E);
    REQUIRE(conn.lastHeader.forward() == false);

    // Validate parsed traffic payload fields
    REQUIRE(conn.trafficSet);
    REQUIRE(conn.lastTrafficPayload.timestampQms() == 10);
    REQUIRE(conn.lastTrafficPayload.flightState() == TrafficPayload::FlightState::ON_GROUND);
    REQUIRE(conn.lastTrafficPayload.aircraftCategory() == TrafficPayload::AircraftCategory::LIGHT_FIXED_WING);
    REQUIRE(conn.lastTrafficPayload.emergencyStatus() == TrafficPayload::EmergencyStatus::NO_EMERGENCY);
    REQUIRE(Catch::Approx(54.107014) == conn.lastTrafficPayload.latitude());
    REQUIRE(Catch::Approx(3.856962) == conn.lastTrafficPayload.longitude());
    REQUIRE(Catch::Approx(55.50).epsilon(0.01) == conn.lastTrafficPayload.speed());
    REQUIRE(Catch::Approx(184.92).epsilon(0.01) == conn.lastTrafficPayload.groundTrack());
    REQUIRE(conn.lastTrafficPayload.altitude() == 7684);
    REQUIRE(Catch::Approx(-3.25).epsilon(0.01) == conn.lastTrafficPayload.verticalRate());
    REQUIRE(conn.lastTrafficPayload.sourceIntegrityLevel() == TrafficPayload::SourceIntegrityLevel::UNDEFINED_SIL);
    REQUIRE(conn.lastTrafficPayload.designAssurance() == TrafficPayload::DesignAssurance::UNDEFINED_NONE);
    REQUIRE(conn.lastTrafficPayload.navigationIntegrity() == TrafficPayload::NavigationIntegrity::RC_7_5_25M);
    REQUIRE(conn.lastTrafficPayload.horizontalPositionAccuracy() == TrafficPayload::HorizontalPositionAccuracy::HFOM_LESS_3M);
    REQUIRE(conn.lastTrafficPayload.verticalPositionAccuracy() == TrafficPayload::VerticalPositionAccuracy::VFOM_LESS_10M);
    REQUIRE(conn.lastTrafficPayload.velocityAccuracy() == TrafficPayload::VelocityAccuracy::ACC_VEL_LESS_1MS);
}

TEST_CASE("Protocol rqSendTrafficPayload", "[Protocol]")
{
    TestConnector conn;
    Protocol protocol(&conn);
    protocol.crcCheckOnReceive = true;

    auto newBuffer = buffer;
    FrameBuffer buf{newBuffer.data(), newBuffer.size()};
    auto result = protocol.handleRx(-42, buf.fullSpan32());
    REQUIRE(result == Protocol::RxStatudeCode::OK);

    protocol.rqSendTrafficPayload((void *)0x1234, false, conn.lastTrafficPayload);

    conn.trafficSet = false;
    result = protocol.handleRx(-42, conn.lastFrame.usedSpan32());

    printf("############### %s", result.c_str());

    REQUIRE(result == Protocol::RxStatudeCode::OK);
    REQUIRE(conn.trafficSet);
    REQUIRE(conn.lastCtx == (void *)0x1234);
}

TEST_CASE("Protocol validation", "[Protocol]")
{
    TestConnector conn;
    Protocol protocol(&conn);
    Header header;
    NetworkPayload network;
    StatusPayload status;
    TrafficPayload traffic;

    protocol.crcCheckOnReceive = true;
    SECTION("OK")
    {
        header.type(Header::PayloadTypeIdentifier::STATUS);
        auto frame = buildRadioPacket(newBufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { status.serialize_issue2(writer); });

        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
    }

    SECTION("UNSUPPORTED_PAYLOAD")
    {
        header.type(Header::PayloadTypeIdentifier::REMOTEID);
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { traffic.serialize_issue1(writer); });

        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::UNSUPPORTED_PAYLOAD);
    }

    SECTION("CRC Error")
    {
        header.type(Header::PayloadTypeIdentifier::REMOTEID);
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { traffic.serialize_issue1(writer); });

        frame[23] = 0; // Set incorrect CRC
        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::CRC_FAILED);
    }

    SECTION("Decryption key 1 unsupported")
    {
        network.keyIndex(1);
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { status.serialize_issue2(writer); });

        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::UNSUPORTED_DECRYPTION_KEY);
    }

    SECTION("Decryption key 3 supported")
    {
        header.type(Header::PayloadTypeIdentifier::STATUS);
        network.keyIndex(3);
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { status.serialize_issue2(writer); });

        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
    }

    SECTION("FEC Not supported")
    {
        network.errorControlMode(NetworkPayload::ErrorControlMode::FEC);
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { status.serialize_issue2(writer); });

        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::UNSUPORTED_ERROR_CONTROL_FEC);
    }

    SECTION("Unsupported protocol version")
    {
        network.protocolVersion(static_cast<NetworkPayload::ProtocolVersion>(3));
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { status.serialize_issue2(writer); });

        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::UNSUPORTED_PROTOCOL_VERSION);
    }
}

TEST_CASE("Protocol Version Default", "[Protocol]")
{
    TestConnector conn;
    Protocol protocol(&conn);
    REQUIRE(protocol.lowestDominotorProtocolVersion() == NetworkPayload::ProtocolVersion::LATEST);
}

TEST_CASE("Protocol Version With Traffic", "[Protocol]")
{
    struct Case
    {
        float lat;
        float lon;
        int alt;
        float speed;
        float track;
        bool sendStatus;
        NetworkPayload::ProtocolVersion statusVersion;
        NetworkPayload::ProtocolVersion trafficVersion;
        NetworkPayload::ProtocolVersion expected;
        const char *name;
    };

    auto tc = GENERATE(

        Case{
            .lat = 53.26f,
            .lon = 4.26f,
            .alt = 6000, // Δalt = 1000 m
            .speed = 50.0f,
            .track = 45.f,
            .sendStatus = false,
            .statusVersion = NetworkPayload::ProtocolVersion::LATEST,
            .trafficVersion = NetworkPayload::ProtocolVersion::LATEST,
            .expected = NetworkPayload::ProtocolVersion::LATEST,
            .name = "Vertical ≥ 800 m blocks downgrade"},

        Case{
            .lat = 53.40f,
            .lon = 4.60f, // far horizontally
            .alt = 5500,  // Δalt = 500 m
            .speed = 50.0f,
            .track = 45.f,
            .sendStatus = false,
            .statusVersion = NetworkPayload::ProtocolVersion::LATEST,
            .trafficVersion = NetworkPayload::ProtocolVersion::LATEST,
            .expected = NetworkPayload::ProtocolVersion::LATEST,
            .name = "Vertical < 800 m but no distance or TCPA trigger"},

        Case{
            .lat = 53.255f,
            .lon = 4.255f, // ~647 m horizontally
            .alt = 5500,   // Δalt = 500 m
            .speed = 50.0f,
            .track = 45.f,
            .sendStatus = false,
            .statusVersion = NetworkPayload::ProtocolVersion::ISSUE_1,
            .trafficVersion = NetworkPayload::ProtocolVersion::ISSUE_1,
            .expected = NetworkPayload::ProtocolVersion::ISSUE_1,
            .name = "Distance < 1 km triggers downgrade"},

        Case{
            .lat = 53.26f,
            .lon = 4.25f, // 1.1Km distance
            .alt = 5500,
            .speed = 50.0f,
            .track = 45.f, // same track as traffic → no closure
            .sendStatus = false,
            .statusVersion = NetworkPayload::ProtocolVersion::LATEST,
            .trafficVersion = NetworkPayload::ProtocolVersion::LATEST,
            .expected = NetworkPayload::ProtocolVersion::LATEST,
            .name = "Parallel flight → TCPA 1569 → no downgrade"},

        Case{
            .lat = 53.268f,
            .lon = 4.25f, // 3Km distance
            .alt = 5500,
            .speed = 50.f,
            .track = 135.f, // opposite of traffic (45° + 180)
            .sendStatus = false,
            .statusVersion = NetworkPayload::ProtocolVersion::ISSUE_1,
            .trafficVersion = NetworkPayload::ProtocolVersion::ISSUE_1,
            .expected = NetworkPayload::ProtocolVersion::ISSUE_1,
            .name = "Head-on closure → TCPA < 30 s triggers downgrade"},

        Case{
            .lat = 53.268f,
            .lon = 4.25f, // 3Km distance
            .alt = 5500,
            .speed = 50.f,
            .track = 135.f, // opposite of traffic (45° + 180)
            .sendStatus = true,
            .statusVersion = NetworkPayload::ProtocolVersion::ISSUE_2,
            .trafficVersion = NetworkPayload::ProtocolVersion::ISSUE_1,
            .expected = NetworkPayload::ProtocolVersion::ISSUE_2,
            // This happens when there was a other aircraft in the vicinity
            // and they transmit at ISSUE_1 byt GATAS did not see that one yet.
            .name = "Head-on closure → TCPA < 30 s triggers ISSUE_2 no downgrade ISSUE_2"},

        Case{
            .lat = 53.35f,
            .lon = 4.35f,
            .alt = 5500,
            .speed = 200.f,
            .track = 35.f, // same direction, faster but opening
            .sendStatus = false,
            .statusVersion = NetworkPayload::ProtocolVersion::LATEST,
            .trafficVersion = NetworkPayload::ProtocolVersion::LATEST,
            .expected = NetworkPayload::ProtocolVersion::LATEST,
            .name = "Diverging flight → TCPA invalid → no downgrade"});

    TestConnector conn;
    Protocol protocol(&conn);

    INFO(tc.name);
    protocol.tick(tc.lat, tc.lon, tc.alt, tc.speed, tc.track);

    if (tc.sendStatus)
    {
        NetworkPayload network;
        network.protocolVersion(tc.statusVersion);
        Header header;
        header.address(Address{0x123456});
        header.type(Header::PayloadTypeIdentifier::STATUS);
        StatusPayload status;
        status.maxProtocolversion(tc.statusVersion);
        auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                      { status.serialize_issue2(writer); });
        auto result = protocol.handleRx(-42, frame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
    }

    NetworkPayload network;
    network.protocolVersion(tc.trafficVersion);
    Header header;
    header.type(Header::PayloadTypeIdentifier::TRAFFIC);
    header.address(Address{0x123456});
    TrafficPayload traffic;
    traffic.latitude(53.25f);
    traffic.longitude(4.25f);
    traffic.altitude(5000);
    traffic.speed(50);
    traffic.groundTrack(45);

    auto frame = buildRadioPacket(bufferSpan, header, network, [&](etl::bit_stream_writer &writer)
                                  { traffic.serialize_issue1(writer); });
    auto result = protocol.handleRx(-42, frame.usedSpan32());

    REQUIRE(result == Protocol::RxStatudeCode::OK);
    REQUIRE(protocol.lowestDominotorProtocolVersion() == tc.expected);
}

TEST_CASE("Protocol Version expiry", "[Protocol]")
{
    using NP = NetworkPayload::ProtocolVersion;

    struct Case
    {
        uint32_t address;
        NetworkPayload::ProtocolVersion maxVersion;
        NetworkPayload::ProtocolVersion networkVersion;
        uint32_t time;
        uint32_t expectedAircraft;
        NetworkPayload::ProtocolVersion expected;
        const char *name;
    };

    auto traffic = etl::make_array<Case>(
        Case{.address = 0x111111, .maxVersion = NP::ISSUE_2, .networkVersion = NP::ISSUE_2, .time = 0'000'000, .expectedAircraft = 1, .expected = NP::ISSUE_2, .name = "Aircraft 1"},
        Case{.address = 0x111111, .maxVersion = NP::ISSUE_2, .networkVersion = NP::ISSUE_2, .time = 10'000'000, .expectedAircraft = 1, .expected = NP::ISSUE_2, .name = "Aircraft 1 again"},
        Case{.address = 0x222222, .maxVersion = NP::ISSUE_1, .networkVersion = NP::ISSUE_2, .time = 30'000'000, .expectedAircraft = 2, .expected = NP::ISSUE_1, .name = "Aircraft lower protcol appears"},
        Case{.address = 0x333333, .maxVersion = NP::ISSUE_2, .networkVersion = NP::ISSUE_2, .time = 05'000'000, .expectedAircraft = 3, .expected = NP::ISSUE_1, .name = "Other aircraft "},
        Case{.address = 0x333333, .maxVersion = NP::ISSUE_2, .networkVersion = NP::ISSUE_2, .time = 20'000'000, .expectedAircraft = 2, .expected = NP::ISSUE_1, .name = "Other aircraft again"},
        Case{.address = 0x333333, .maxVersion = NP::ISSUE_2, .networkVersion = NP::ISSUE_2, .time = 20'000'000, .expectedAircraft = 1, .expected = NP::ISSUE_2, .name = "0x333333"},
        Case{.address = 0x444444, .maxVersion = NP::ISSUE_1, .networkVersion = NP::ISSUE_1, .time = 20'000'000, .expectedAircraft = 2, .expected = NP::ISSUE_2, .name = "one new aircraft"},
        Case{.address = 0x444444, .maxVersion = NP::ISSUE_1, .networkVersion = NP::ISSUE_1, .time = 20'000'000, .expectedAircraft = 1, .expected = NP::ISSUE_2, .name = "one aircraft"});

    TestConnector conn;
    Protocol protocol(&conn);

    for (auto const &t : traffic)
    {
        INFO("Case: " << t.name);
        CAPTURE(t.address);
        CAPTURE(t.time);
        CAPTURE(static_cast<int>(t.expectedAircraft));
        CAPTURE(static_cast<int>(t.expected));

        conn.tick += t.time;
        protocol.tick(53.268f, 4.25f, 5500, 50.f, 45.f);

        NetworkPayload network;
        network.protocolVersion(NP::ISSUE_2);

        Header header;
        header.address(Address{t.address});
        header.type(Header::PayloadTypeIdentifier::STATUS);

        StatusPayload status;
        status.maxProtocolversion(NP::ISSUE_2);

        auto frame = buildRadioPacket(bufferSpan, header, network,
                                      [&](etl::bit_stream_writer &writer)
                                      {
                                          status.serialize_issue2(writer);
                                      });

        auto result = protocol.handleRx(-42, frame.usedSpan32());

        REQUIRE(result == Protocol::RxStatudeCode::OK);
        REQUIRE(protocol.numberOfNeighbours() == t.expectedAircraft);
    }
}

// Helper: extract the NetworkPayload protocol version from the first byte of a sent frame.
// The first byte is stored as etl::reverse_bits(networkPayload.asUint8()); version occupies bits 0–3 of asUint8.
static NetworkPayload::ProtocolVersion frameNetVersion(const FrameBuffer &frame)
{
    return static_cast<NetworkPayload::ProtocolVersion>(etl::reverse_bits<uint8_t>(frame[0]) & 0x0F);
}

// Build a TRAFFIC packet received from a neighbour at a given position / protocol version.
static FrameBuffer makeTrafficFrame(etl::span<uint32_t> buf,
                                    uint32_t addr,
                                    float lat, float lon, int32_t alt,
                                    NetworkPayload::ProtocolVersion version)
{
    NetworkPayload network;
    network.protocolVersion(version);
    Header hdr;
    hdr.address(Address{addr});
    hdr.type(Header::PayloadTypeIdentifier::TRAFFIC);
    TrafficPayload tp;
    tp.latitude(lat);
    tp.longitude(lon);
    tp.altitude(alt);
    tp.speed(50.f);
    tp.groundTrack(45.f);
    return buildRadioPacket(buf, hdr, network, [&](etl::bit_stream_writer &w) { tp.serialize_issue1(w); });
}

TEST_CASE("Protocol send payload type and network version", "[Protocol]")
{
    using NP = NetworkPayload::ProtocolVersion;

    SECTION("traffic uses ISSUE_2 by default — no neighbours")
    {
        // Do NOT call tick: doSendStatus stays false, no neighbours → lowestDominator = ISSUE_2.
        TestConnector conn;
        Protocol protocol(&conn);

        TrafficPayload tp;
        protocol.rqSendTrafficPayload((void *)0x1, false, tp);

        REQUIRE(frameNetVersion(conn.lastFrame) == NP::ISSUE_2);

        conn.trafficSet = false;
        conn.statusSet = false;
        auto result = protocol.handleRx(-42, conn.lastFrame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.trafficSet);
        REQUIRE_FALSE(conn.statusSet);
        REQUIRE(conn.lastHeader.type() == Header::PayloadTypeIdentifier::TRAFFIC);
    }

    SECTION("traffic uses ISSUE_1 when a close ISSUE_1 neighbour is tracked")
    {
        TestConnector conn;
        Protocol protocol(&conn);

        // Ownship at (53.255, 4.255); neighbour at (53.25, 4.25) — ~647 m, Δalt 500 m → triggers downgrade.
        // tick() sets doSendStatus=true immediately (nextStatusSent=0); consume it so the
        // next rqSendTrafficPayload call sends TRAFFIC, not STATUS.
        protocol.tick(53.255f, 4.255f, 5500, 50.f, 45.f);
        {
            TrafficPayload dummy;
            protocol.rqSendTrafficPayload((void *)0x0, false, dummy); // sends STATUS, clears doSendStatus
        }

        // Inject a close ISSUE_1 neighbour (~647 m away, Δalt 500 m → triggers downgrade).
        auto rxFrame = makeTrafficFrame(bufferSpan, 0x123456, 53.25f, 4.25f, 5000, NP::ISSUE_1);
        REQUIRE(protocol.handleRx(-42, rxFrame.usedSpan32()) == Protocol::RxStatudeCode::OK);
        REQUIRE(protocol.lowestDominotorProtocolVersion() == NP::ISSUE_1);

        // doSendStatus is now false → rqSendTrafficPayload sends TRAFFIC at lowestDominator (ISSUE_1).
        TrafficPayload tp;
        protocol.rqSendTrafficPayload((void *)0x1, false, tp);

        REQUIRE(frameNetVersion(conn.lastFrame) == NP::ISSUE_1);

        conn.trafficSet = false;
        conn.statusSet = false;
        REQUIRE(protocol.handleRx(-42, conn.lastFrame.usedSpan32()) == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.trafficSet);
        REQUIRE_FALSE(conn.statusSet);
        REQUIRE(conn.lastHeader.type() == Header::PayloadTypeIdentifier::TRAFFIC);
    }

    SECTION("status payload always uses ISSUE_2 even when lowestDominator is ISSUE_1")
    {
        TestConnector conn;
        Protocol protocol(&conn);

        // Ownship at (53.255, 4.255); neighbour at (53.25, 4.25) — ~647 m, Δalt 500 m → triggers downgrade.
        // tick() sets doSendStatus=true and establishes ownship position.
        protocol.tick(53.255f, 4.255f, 5500, 50.f, 45.f);

        // Inject a close ISSUE_1 neighbour so lowestDominator would be ISSUE_1 for TRAFFIC.
        auto rxFrame = makeTrafficFrame(bufferSpan, 0x123456, 53.25f, 4.25f, 5000, NP::ISSUE_1);
        REQUIRE(protocol.handleRx(-42, rxFrame.usedSpan32()) == Protocol::RxStatudeCode::OK);
        REQUIRE(protocol.lowestDominotorProtocolVersion() == NP::ISSUE_1);

        // doSendStatus=true → rqSendTrafficPayload sends STATUS with ISSUE_2 (not TRAFFIC with ISSUE_1).
        TrafficPayload tp;
        protocol.rqSendTrafficPayload((void *)0x1, false, tp);

        REQUIRE(frameNetVersion(conn.lastFrame) == NP::ISSUE_2);

        conn.trafficSet = false;
        conn.statusSet = false;
        REQUIRE(protocol.handleRx(-42, conn.lastFrame.usedSpan32()) == Protocol::RxStatudeCode::OK);
        REQUIRE_FALSE(conn.trafficSet);
        REQUIRE(conn.statusSet);
        REQUIRE(conn.lastHeader.type() == Header::PayloadTypeIdentifier::STATUS);
    }
}

TEST_CASE("Uplink identifies the transmitting station and both original senders", "[Protocol]")
{
    struct Receiver : TestConnector
    {
        bool received = false;

        void adsl_receivedUplinkTraffic(const Header &header, etl::span<const UplinkEntry> entries) override
        {
            received = true;
            // Issue 2 F.2 and G.3: station in the header, original senders in the entries.
            REQUIRE(5 == header.addressMappingTable());
            REQUIRE(0xACEACEU == header.address().asUint());
            REQUIRE(2U == entries.size());
            REQUIRE(5 == entries[0].addressMappingTable());
            REQUIRE(0x123456U == entries[0].address().asUint());
            REQUIRE(6 == entries[1].addressMappingTable());
            REQUIRE(0x654321U == entries[1].address().asUint());
        }
    } receiver;

    TestConnector sender;
    Protocol tx(&sender);
    Protocol rx(&receiver);
    tx.setAddress(0xACEACE);
    rx.crcCheckOnReceive = true;
    const etl::array<UplinkEntry, 2> entries{
        UplinkEntry{5, 0x123456U, TrafficPayload{}},
        UplinkEntry{6, 0x654321U, TrafficPayload{}}};
    tx.rqSendUplinkPayload(nullptr, false, entries);
    REQUIRE(Protocol::RxStatudeCode::OK == rx.handleRx(-42, sender.lastFrame.usedSpan32()));
    REQUIRE(receiver.received);
}

TEST_CASE("Protocol rqSendUplinkPayload round-trip", "[Protocol]")
{
    // crcCheckOnReceive is intentionally left false: word-padding in the test
    // harness can cause residue mismatches for non-word-aligned payloads.
    // In production the radio provides the exact byte count before handleRx is called.
    TestConnector conn;
    Protocol protocol(&conn);

    auto makeEntry = [](uint32_t addr, float lat, float lon, int32_t alt, float speed, float track) {
        TrafficPayload tp;
        tp.latitude(lat);
        tp.longitude(lon);
        tp.altitude(alt);
        tp.speed(speed);
        tp.groundTrack(track);
        tp.flightState(TrafficPayload::FlightState::AIRBORNE);
        UplinkEntry entry;
        entry.addressMappingTable(0x05);
        entry.address(Address{addr});
        entry.payload(tp);
        return entry;
    };

    SECTION("single target")
    {
        etl::array<UplinkEntry, 1> entries = {makeEntry(0xAB1234, 54.107f, 3.857f, 7685, 55.5f, 185.0f)};
        protocol.rqSendUplinkPayload((void *)0xABCD, false, etl::span<const UplinkEntry>(entries));

        REQUIRE(conn.lastCtx == (void *)0xABCD);

        conn.uplinkCount = 0;
        auto result = protocol.handleRx(-42, conn.lastFrame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.uplinkCount == 1);
        REQUIRE(Catch::Approx(54.107f).epsilon(0.001f) == conn.lastUplinkEntry.payload().latitude());
        REQUIRE(conn.lastUplinkEntry.payload().flightState() == TrafficPayload::FlightState::AIRBORNE);
        REQUIRE(conn.lastUplinkEntry.address() == Address{0xAB1234});
        REQUIRE(conn.lastUplinkEntry.addressMappingTable() == 0x05);
    }

    SECTION("five targets")
    {
        etl::array<UplinkEntry, 5> entries = {
            makeEntry(0x000001, 54.0f, 3.8f, 1000, 50.0f, 90.0f),
            makeEntry(0x000002, 54.1f, 3.9f, 2000, 51.0f, 91.0f),
            makeEntry(0x000003, 54.2f, 4.0f, 3000, 52.0f, 92.0f),
            makeEntry(0x000004, 54.3f, 4.1f, 4000, 53.0f, 93.0f),
            makeEntry(0x000005, 54.4f, 4.2f, 5000, 54.0f, 94.0f),
        };
        protocol.rqSendUplinkPayload((void *)0x1111, false, etl::span<const UplinkEntry>(entries));

        conn.uplinkCount = 0;
        auto result = protocol.handleRx(-42, conn.lastFrame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.uplinkCount == 5);
    }

    SECTION("ten targets — maximum")
    {
        etl::array<UplinkEntry, 10> entries;
        for (size_t i = 0; i < 10; ++i)
        {
            entries[i] = makeEntry(static_cast<uint32_t>(0x100000 + i),
                                   54.0f + i * 0.01f, 3.8f,
                                   static_cast<int32_t>(1000 * (i + 1)), 50.0f, 0.0f);
        }
        protocol.rqSendUplinkPayload((void *)0x2222, false, etl::span<const UplinkEntry>(entries));

        conn.uplinkCount = 0;
        auto result = protocol.handleRx(-42, conn.lastFrame.usedSpan32());
        REQUIRE(result == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.uplinkCount == 10);
        REQUIRE(Catch::Approx(54.09f).epsilon(0.001f) == conn.lastUplinkEntry.payload().latitude());
    }

    SECTION("empty span is ignored")
    {
        conn.lastCtx = nullptr;
        protocol.rqSendUplinkPayload((void *)0x9999, false, etl::span<const UplinkEntry>{});
        REQUIRE(conn.lastCtx == nullptr);
    }
}

// Helper: strip the leading length byte from a frame sent with addPayloadLength=true,
// then loopback via handleRx.  Returns the handleRx result code.
static Protocol::RxStatudeCode loopbackWithLengthByte(Protocol &protocol, FrameBuffer &frame)
{
    auto bytes = frame.usedSpan();
    etl::mem_move(bytes.data() + 1, bytes.size() - 1, bytes.data());
    auto stripped32 = etl::span<uint32_t>(reinterpret_cast<uint32_t *>(bytes.data()), (bytes.size() - 1) / sizeof(uint32_t));
    return protocol.handleRx(-42, stripped32);
}

TEST_CASE("Protocol addPayloadLength prepends length byte", "[Protocol]")
{
    // NetworkPayload(1) + Header(5) + TrafficPayload(15) + CRC(3) = 24 bytes
    constexpr size_t TRAFFIC_SIZE = NetworkPayload::LENGTH + Header::LENGTH + TrafficPayload::LENGTH + NetworkPayload::LENGTH_CRC;
    // NetworkPayload(1) + Header(5) + UplinkEntry::LENGTH(19) padded(0) + CRC(3) = 28 bytes
    const size_t UPLINK_1_SIZE = NetworkPayload::LENGTH + Header::LENGTH + UplinkEntry::calculateSizeWithPadding(1) + NetworkPayload::LENGTH_CRC;

    SECTION("traffic: length byte present and frame round-trips after stripping")
    {
        TestConnector conn;
        Protocol protocol(&conn);

        TrafficPayload tp;
        protocol.rqSendTrafficPayload((void *)0x1, true, tp);

        // Frame should be sizeBytes+1 bytes total; first byte is the length.
        REQUIRE(conn.lastFrame.used_bytes == TRAFFIC_SIZE + 1);
        REQUIRE(conn.lastFrame[0] == static_cast<uint8_t>(TRAFFIC_SIZE));

        // After stripping the length byte the payload must decode correctly.
        conn.trafficSet = false;
        REQUIRE(loopbackWithLengthByte(protocol, conn.lastFrame) == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.trafficSet);
        REQUIRE(conn.lastHeader.type() == Header::PayloadTypeIdentifier::TRAFFIC);
    }

    SECTION("uplink: length byte present and entries round-trip after stripping")
    {
        TestConnector conn;
        Protocol protocol(&conn);

        TrafficPayload tp;
        tp.latitude(54.1f);
        tp.longitude(3.9f);
        tp.altitude(2000);
        tp.speed(51.f);
        tp.groundTrack(91.f);
        tp.flightState(TrafficPayload::FlightState::AIRBORNE);
        etl::array<UplinkEntry, 1> entries;
        entries[0].addressMappingTable(0x05);
        entries[0].address(Address{0xAB1234});
        entries[0].payload(tp);

        protocol.rqSendUplinkPayload((void *)0x1, true, etl::span<const UplinkEntry>(entries));

        REQUIRE(conn.lastFrame.used_bytes == UPLINK_1_SIZE + 1);
        REQUIRE(conn.lastFrame[0] == static_cast<uint8_t>(UPLINK_1_SIZE));

        conn.uplinkCount = 0;
        REQUIRE(loopbackWithLengthByte(protocol, conn.lastFrame) == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.uplinkCount == 1);
        REQUIRE(conn.lastUplinkEntry.address() == Address{0xAB1234});
        REQUIRE(Catch::Approx(54.1f).epsilon(0.001f) == conn.lastUplinkEntry.payload().latitude());
    }

    SECTION("addPayloadLength=false sends sizeBytes without length prefix")
    {
        TestConnector conn;
        Protocol protocol(&conn);

        TrafficPayload tp;
        protocol.rqSendTrafficPayload((void *)0x1, false, tp);

        REQUIRE(conn.lastFrame.used_bytes == TRAFFIC_SIZE);

        conn.trafficSet = false;
        REQUIRE(protocol.handleRx(-42, conn.lastFrame.usedSpan32()) == Protocol::RxStatudeCode::OK);
        REQUIRE(conn.trafficSet);
    }
}