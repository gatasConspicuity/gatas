#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include "task.h"
#include "hardware/gpio.h"
#include "pico/time.h"

namespace RadioTest
{
inline bool busy = false;
inline bool shortTransfer = false;
inline std::vector<std::vector<uint8_t>> writes;
inline std::vector<size_t> reads;
inline std::vector<uint8_t> response;
inline size_t responseOffset = 0;
inline std::vector<char> transfers;
inline std::vector<uint32_t> notifications;
inline size_t notificationIndex = 0;
struct EndTask {};
inline void reset()
{
    busy = false; shortTransfer = false; writes.clear(); reads.clear();
    response.clear(); responseOffset = 0; transfers.clear(); notifications.clear(); notificationIndex = 0;
}
}
#define GPIO_OUT 1
#define GPIO_IN 0
#define GPIO_IRQ_EDGE_RISE 1
#define NUM_BANK0_GPIOS 30
inline void gpio_init(uint) {}
inline void gpio_set_dir(uint, bool) {}
inline void gpio_put(uint, bool) {}
inline void gpio_pull_up(uint) {}
inline bool gpio_get(uint) { if (RadioTest::busy) { time_us_Value += 1000; } return RadioTest::busy; }
inline void busy_wait_us_32(uint32_t) {}
inline int xTaskGetSchedulerState() { return 1; }
#define taskSCHEDULER_RUNNING 1
#define taskYIELD() ((void)0)
inline void xTaskNotifyGive(TaskHandle_t) {}
inline int testNotifyWait(uint32_t, uint64_t, uint32_t *value, TickType_t)
{
    if (RadioTest::notificationIndex >= RadioTest::notifications.size()) { throw RadioTest::EndTask{}; }
    *value = RadioTest::notifications[RadioTest::notificationIndex++];
    return pdPASS;
}
#define xTaskNotifyWait testNotifyWait
#define bi_decl(x)
#define bi_1pin_with_name(a,b)
inline void *spi0 = nullptr;
inline void *spi1 = reinterpret_cast<void *>(1);
inline int spi_write_blocking(void *, const uint8_t *data, size_t length)
{
    RadioTest::transfers.push_back('W');
    RadioTest::writes.emplace_back(data, data + length);
    return RadioTest::shortTransfer ? 0 : static_cast<int>(length);
}
inline int spi_read_blocking(void *, uint8_t fill, uint8_t *data, size_t length)
{
    if (fill != 0) { throw std::runtime_error("nonzero MOSI on read"); }
    RadioTest::transfers.push_back('R');
    RadioTest::reads.push_back(length);
    for (size_t i = 0; i < length; ++i)
    {
        data[i] = RadioTest::responseOffset < RadioTest::response.size() ? RadioTest::response[RadioTest::responseOffset++] : 0;
    }
    return RadioTest::shortTransfer ? 0 : static_cast<int>(length);
}
