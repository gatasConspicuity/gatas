#include <catch2/catch_test_macros.hpp>
#include "picopio.hpp"

TEST_CASE("PIO allocation publishes only complete acquisitions", "[startup][pio]")
{
    pio0State = PioMock{};
    pio1State = PioMock{};
    const pio_program_t program;
    PIO selected = pio0;
    int sm = 2;
    uint offset = 12;

    SECTION("no program space leaves no handles or allocations")
    {
        pio0State.programFits = false;
        pio1State.programFits = false;
        REQUIRE_FALSE(add_pio_program(&program, &selected, &sm, &offset));
        REQUIRE(selected == nullptr);
        REQUIRE(sm == -1);
        REQUIRE(offset == 0);
        REQUIRE(pio0State.claimedStateMachines == 0);
        REQUIRE(pio1State.claimedStateMachines == 0);
    }
    SECTION("no state machines loads no program")
    {
        pio0State.freeSm = -1;
        pio1State.freeSm = -1;
        REQUIRE_FALSE(add_pio_program(&program, &selected, &sm, &offset));
        REQUIRE(selected == nullptr);
        REQUIRE(sm == -1);
        REQUIRE(offset == 0);
        REQUIRE(pio0State.loadedPrograms == 0);
        REQUIRE(pio1State.loadedPrograms == 0);
    }
    SECTION("second PIO is used when the first has no state machine")
    {
        pio0State.freeSm = -1;
        pio1State.freeSm = 3;
        REQUIRE(add_pio_program(&program, &selected, &sm, &offset));
        REQUIRE(selected == pio1);
        REQUIRE(sm == 3);
        REQUIRE(offset == pio1State.offset);
        REQUIRE(pio0State.loadedPrograms == 0);
        REQUIRE(pio1State.loadedPrograms == 1);
    }
    SECTION("second PIO is used when the first has no instruction space")
    {
        pio0State.programFits = false;
        REQUIRE(add_pio_program(&program, &selected, &sm, &offset));
        REQUIRE(selected == pio1);
        REQUIRE(pio0State.claimedStateMachines == 0);
    }
    SECTION("first PIO succeeds without touching the second")
    {
        REQUIRE(add_pio_program(&program, &selected, &sm, &offset));
        REQUIRE(selected == pio0);
        REQUIRE(sm == 0);
        REQUIRE(offset == pio0State.offset);
        REQUIRE(pio1State.claimedStateMachines == 0);
        REQUIRE(pio1State.loadedPrograms == 0);
    }
}
