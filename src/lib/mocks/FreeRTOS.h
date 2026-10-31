#pragma once

#include <stdint.h>
#include <cstdlib>

inline bool mockPortMallocFail = false;
inline size_t mockPortMallocCount = 0;
inline size_t mockPortFreeCount = 0;

inline void *pvPortMalloc(size_t size)
{
    if (mockPortMallocFail)
    {
        return nullptr;
    }
    auto *ptr = std::malloc(size);
    if (ptr != nullptr)
    {
        ++mockPortMallocCount;
    }
    return ptr;
}

inline void vPortFree(void *ptr)
{
    if (ptr != nullptr)
    {
        ++mockPortFreeCount;
        std::free(ptr);
    }
}

#include "FreeRTOSconfig.h"

#define pdMS_TO_TICKS( x )     ( ( x ) / portTICK_PERIOD_MS )
#define BaseType_t int
#define TickType_t uint32_t
typedef void (* TaskFunction_t)( void * );
typedef void (* TimerCallbackFunction_t)( void * );

#define UBaseType_t uint32_t
#define pdPASS 0
#define pdFALSE 0
#define pdFAIL 0
#define pdTRUE 1
#define StaticTask_t int
#define StackType_t uint8_t
#define StaticTimer_t int
#define StaticSemaphore_t int
typedef void* SemaphoreHandle_t;

struct tskTaskControlBlock; /* The old naming convention is used to prevent breaking kernel aware debuggers. */
typedef struct tskTaskControlBlock         *TaskHandle_t;


#define tskIDLE_PRIORITY 0
#define tskDEFAULT_INDEX_TO_NOTIFY 0

typedef enum
{
    eNoAction = 0,            /* Notify the task without updating its notify value. */
    eSetBits,                 /* Set bits in the task's notification value. */
    eIncrement,               /* Increment the task's notification value. */
    eSetValueWithOverwrite,   /* Set the task's notification value to a specific value even if the previous value has not yet been read by the task. */
    eSetValueWithoutOverwrite /* Set the task's notification value if the previous value has been read by the task. */
} eNotifyAction;


#define taskENTER_CRITICAL_FROM_ISR() (0)
#define taskEXIT_CRITICAL_FROM_ISR(saved) ((void)(saved))
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portMAX_DELAY              ( TickType_t ) 0xffffffffUL
