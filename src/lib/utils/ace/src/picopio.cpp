#include "../picopio.hpp"
#include <initializer_list>

/** Find a free PIO and state machine, publishing handles only on success. */
bool add_pio_program(const pio_program_t *program, PIO *pio_hw, int *sm, uint *offset)
{
    *pio_hw = nullptr;
    *sm = -1;
    *offset = 0;
#if defined(PICO_RP2040) || defined(PICO_RP2350)
    for (auto candidate : {pio0, pio1})
    {
        if (!pio_can_add_program(candidate, program))
        {
            continue;
        }
        const int stateMachine = pio_claim_unused_sm(candidate, false);
        if (stateMachine < 0)
        {
            continue;
        }
        const uint programOffset = pio_add_program(candidate, program);
        *pio_hw = candidate;
        *sm = stateMachine;
        *offset = programOffset;
        return true;
    }
#endif
    return false;
}
