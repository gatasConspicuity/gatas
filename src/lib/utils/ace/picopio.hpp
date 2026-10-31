#pragma once

#include "hardware/pio.h"
#include "stdint.h"

// Find a free pio and state machine and load the program into it.
// On failure, returns false with null PIO, state machine -1, and offset 0.
bool add_pio_program(const pio_program_t *program, PIO *pio_hw, int *sm, uint *offset);

