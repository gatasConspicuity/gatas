#pragma once
#include <stdint.h>

inline void watchdog_enable(uint32_t, bool) {}
inline void watchdog_update() {}

void watchdog_reboot(uint8_t, uint8_t, uint8_t) {

}

void tight_loop_contents() {

}

void reset_usb_boot(uint8_t, uint8_t) {

}
