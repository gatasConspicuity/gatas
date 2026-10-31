#pragma once

#include <cstdint>
#include <cstdio>
#include "FreeRTOS.h"

using EventGroupHandle_t = void*;
using EventBits_t = uint32_t;
using TickType_t = uint32_t;

#define pdTRUE  1
#define pdFALSE 0
// #define portMAX_DELAY 0xFFFFFFFF

// Mock handles
inline EventGroupHandle_t lastCreatedEventGroup = reinterpret_cast<void*>(0x12345678);
inline bool deletedGroupCalled = false;

inline EventGroupHandle_t xEventGroupCreate() {
    printf("xEventGroupCreate() called\n");
    return lastCreatedEventGroup;
}

inline void vEventGroupDelete(EventGroupHandle_t handle) {
    printf("vEventGroupDelete() called on %p\n", handle);
    deletedGroupCalled = true;
}

inline EventBits_t mockEventWaitMask = UINT32_MAX;
inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t,
                                       EventBits_t bitsToWaitFor,
                                       BaseType_t clearOnExit,
                                       BaseType_t waitForAll,
                                       TickType_t ticksToWait) {
    printf("xEventGroupWaitBits() called (bits=0x%X)\n", bitsToWaitFor);
    return bitsToWaitFor & mockEventWaitMask;
}

inline EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t bitsToSet) {
    printf("xEventGroupSetBits() called (bits=0x%X)\n", bitsToSet);
    return bitsToSet;
}

inline EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t bitsToClear) {
    printf("xEventGroupClearBits() called (bits=0x%X)\n", bitsToClear);
    return 0;
}
