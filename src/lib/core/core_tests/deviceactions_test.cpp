#include <catch2/catch_test_macros.hpp>
#include "ace/binarymessages.hpp"
#include "../../gatasconnect/ace/cobsstreamhandler.hpp"

TEST_CASE("Device action requests use the current compact wire contract", "[device-actions]")
{
    for (const uint8_t action : {1, 2, 3})
    {
        const uint8_t raw[] = {12, action, 0xab};
        const auto request = BinaryMessages::deserializeDeviceActionRequestV1(raw);
        REQUIRE(request.has_value());
        REQUIRE(static_cast<uint8_t>(request->action) == action);
        REQUIRE(request->bootId == 0xab);
    }
    const uint8_t invalid[][3] = {{12, 0, 0xab}, {12, 4, 0xab}, {13, 1, 0xab}};
    for (const auto &raw : invalid)
    {
        REQUIRE_FALSE(BinaryMessages::deserializeDeviceActionRequestV1(raw));
    }
    const uint8_t raw[] = {12, 1, 0xab, 0};
    REQUIRE_FALSE(BinaryMessages::deserializeDeviceActionRequestV1(etl::span<const uint8_t>(raw, 2)));
    REQUIRE_FALSE(BinaryMessages::deserializeDeviceActionRequestV1(raw));
    const uint8_t legacy[16] = {12, 1};
    REQUIRE_FALSE(BinaryMessages::deserializeDeviceActionRequestV1(legacy));
}

namespace
{
    class ActionReceiver : public etl::message_router<ActionReceiver, GATAS::DeviceActionRequestMsg>
    {
    public:
        size_t count = 0;
        GATAS::DeviceAction action = GATAS::DeviceAction::Status;
        void on_receive(const GATAS::DeviceActionRequestMsg &msg)
        {
            ++count;
            action = msg.action;
        }
        void on_receive_unknown(const etl::imessage &) {}
    };
}

TEST_CASE("Device actions require Bluetooth and the current boot ID even during holdback", "[device-actions]")
{
    etl::message_bus<4> bus;
    GATAS::OwnshipState ownship;
    CobsStreamHandler handler(bus, ownship);
    ActionReceiver receiver;
    bus.subscribe(receiver);
    auto send = [&](GATAS::GatasConnectTransport transport, uint8_t expectedBootId, bool allowNonDeviceActionFrames)
    {
        const uint8_t raw[] = {12, 2, 0xab};
        uint8_t framed[8] = {};
        const auto size = encodeCOBS(raw, sizeof(raw), framed, sizeof(framed), true);
        REQUIRE(size > 0);
        handler.handle(0.0f, 0.0f, etl::span<uint8_t>(framed, size), transport, expectedBootId, allowNonDeviceActionFrames);
    };
    send(GATAS::GatasConnectTransport::UDP, 0xab, true);
    REQUIRE(receiver.count == 0);
    send(GATAS::GatasConnectTransport::Bluetooth, 0xac, true);
    REQUIRE(receiver.count == 0);
    send(GATAS::GatasConnectTransport::Bluetooth, 0xab, false);
    REQUIRE(receiver.count == 1);
    REQUIRE(receiver.action == GATAS::DeviceAction::Restart);
}
