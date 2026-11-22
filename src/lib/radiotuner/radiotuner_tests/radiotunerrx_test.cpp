
#include <catch2/catch_test_macros.hpp>

#define private public

#include "../ace/radiotunerrx.hpp"
#include "ace/messages.hpp"
#include "pico/time.h"
#include "etl/vector.h"

using RadioProtocolCtx = RadioTunerRx::RadioProtocolCtx;
GATAS::ThreadSafeBus<50> bus;
GATAS::OwnshipPositionMsg ownshipPosition{};

TEST_CASE("RadioProtocolCtx", "[single-file]")
{
    RadioProtocolCtx ctx{nullptr, nullptr};

    etl::array<uint8_t, GATAS::RADIO_PROTOCOLS.size()> slotReceived = {};

    ctx.updateConspicuityProtocols(etl::vector<GATAS::ConspicuityProtocol, 1> {});
    ctx.prioritizeRxTimings();
    REQUIRE(etl::vector<GATAS::ConspicuityProtocol, 1> {} == ctx.protocolTimeSlots);

    SECTION("Prioritise should return 500ms on default zone", "[single-file]")
    {
        REQUIRE( ctx.advanceReceiveSlot(CountryRegulations::ZONE0) == 500);
    }
    SECTION("extra FLARM Data Source", "[single-file]")
    {
        ctx.updateConspicuityProtocols(etl::vector{GATAS::ConspicuityProtocol::FLARM});
        ctx.prioritizeRxTimings();
        REQUIRE(etl::vector{GATAS::ConspicuityProtocol::FLARM} == ctx.protocolTimeSlots);
    }

    SECTION("3 Data Sources", "[single-file]")
    {
        ctx.updateConspicuityProtocols(etl::vector{GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::ADSL_M_BAND});
        ctx.prioritizeRxTimings();
        REQUIRE(etl::vector{GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::ADSL_M_BAND} == ctx.protocolTimeSlots);

        SECTION("FLARM Data Received", "[single-file]")
        {
            slotReceived[GATAS::radioProtocolIndex(GATAS::ConspicuityProtocol::FLARM)] += 1;
            ctx.updateSlotReceive(slotReceived);
            ctx.prioritizeRxTimings();
            REQUIRE(etl::vector{GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::ADSL_M_BAND} == ctx.protocolTimeSlots);

            SECTION("OGN and FLARM Data Received", "[single-file]")
            {
                slotReceived[GATAS::radioProtocolIndex(GATAS::ConspicuityProtocol::FLARM)] += 1;
                slotReceived[GATAS::radioProtocolIndex(GATAS::ConspicuityProtocol::OGN)] += 1;
                ctx.updateSlotReceive(slotReceived);
                ctx.prioritizeRxTimings();
                REQUIRE(etl::vector{GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::ADSL_M_BAND} == ctx.protocolTimeSlots);
                REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::FLARM);

                SECTION("Should be circular receive slots", "[single-file]")
                {
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::FLARM);
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::OGN);
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::OGN);
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::ADSL_M_BAND);
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    // Here prioritsation should happen
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::FLARM);
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::FLARM);
                    ctx.advanceReceiveSlot(CountryRegulations::Zone::ZONE1);
                    REQUIRE(*ctx.upcomingConspicuityProtocol == GATAS::ConspicuityProtocol::OGN);
                }
            }

            SECTION("OGN Data, no FLARM Received", "[single-file]")
            {
                slotReceived[GATAS::radioProtocolIndex(GATAS::ConspicuityProtocol::OGN)] += 1;
                ctx.updateSlotReceive(slotReceived);
                ctx.prioritizeRxTimings();
                REQUIRE(etl::vector{GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::ADSL_M_BAND} == ctx.protocolTimeSlots);

                SECTION("NO Data Received", "[single-file]")
                {
                    ctx.updateSlotReceive(slotReceived);
                    ctx.prioritizeRxTimings();
                    REQUIRE(etl::vector{GATAS::ConspicuityProtocol::FLARM, GATAS::ConspicuityProtocol::OGN, GATAS::ConspicuityProtocol::ADSL_M_BAND} == ctx.protocolTimeSlots);
                }

                SECTION("Data sources removed", "[single-file]")
                {
                    ctx.updateConspicuityProtocols(etl::vector<GATAS::ConspicuityProtocol, 1> {});
                    ctx.prioritizeRxTimings();
                    REQUIRE(etl::vector{etl::vector<GATAS::ConspicuityProtocol, 1>{}} == ctx.protocolTimeSlots);
                }
            }
        }
    }
}

