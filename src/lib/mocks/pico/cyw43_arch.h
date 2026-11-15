#pragma once

#define CYW43_WL_GPIO_LED_PIN 0

inline void *cyw43_arch_async_context()
{
    return nullptr;
}

inline void cyw43_arch_gpio_put(int, bool) {}
