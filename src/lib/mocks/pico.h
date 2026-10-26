#pragma once

#include <stdint.h>
#include <stdio.h>

typedef unsigned int uint;




// mock panic
inline void panic_unsupported(const char *file, int line, const char *func, const char *msg)
{
    printf("panic_unsupported: %s:%d %s: %s\n", file, line, func, msg);
}


inline void panic(const char *msg)
{
    printf("panic: %s\n", msg);
}

inline uint8_t get_core_num() {
    return 0;
}

#define __time_critical_func(func_name) func_name

#define __scratch_y(func_name) 

#define __force_inline 

#define __isr 

#define __in_flash() 

#define __uninitialized_ram

// Not sure whee to put it, this is the MEASURE Macro from core
#define GATAS_MEASURE(name, ...) (void(0))
#define GATAS_MEASURE_M(name, ...) auto measure = false
