#pragma once
#include <cassert>
#include <cstdint>
using uint = unsigned int;
struct PioMock
{
    bool programFits = true;
    int freeSm = 0;
    uint offset = 7;
    int loadedPrograms = 0;
    int claimedStateMachines = 0;
};
using PIO = PioMock *;
struct pio_program_t {};
inline PioMock pio0State;
inline PioMock pio1State;
inline PIO pio0 = &pio0State;
inline PIO pio1 = &pio1State;
inline bool pio_can_add_program(PIO pio, const pio_program_t *) { return pio->programFits; }
inline int pio_claim_unused_sm(PIO pio, bool required)
{
    assert(!required);
    if (pio->freeSm >= 0)
    {
        ++pio->claimedStateMachines;
    }
    return pio->freeSm;
}
inline uint pio_add_program(PIO pio, const pio_program_t *)
{
    assert(pio->programFits);
    assert(pio->claimedStateMachines > 0);
    ++pio->loadedPrograms;
    return pio->offset;
}
