#pragma once

#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"

/* IDs for commands that can be sent/received on the timer queue.  These are to
 * be used solely through the macros that make up the public software timer API,
 * as defined below.  The commands that are sent from interrupts must use the
 * highest numbers as tmrFIRST_FROM_ISR_COMMAND is used to determine if the task
 * or interrupt version of the queue send function should be used. */
#define tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR    ( ( BaseType_t ) -2 )
#define tmrCOMMAND_EXECUTE_CALLBACK             ( ( BaseType_t ) -1 )
#define tmrCOMMAND_START_DONT_TRACE             ( ( BaseType_t ) 0 )
#define tmrCOMMAND_START                        ( ( BaseType_t ) 1 )
#define tmrCOMMAND_RESET                        ( ( BaseType_t ) 2 )
#define tmrCOMMAND_STOP                         ( ( BaseType_t ) 3 )
#define tmrCOMMAND_CHANGE_PERIOD                ( ( BaseType_t ) 4 )
#define tmrCOMMAND_DELETE                       ( ( BaseType_t ) 5 )

#define tmrFIRST_FROM_ISR_COMMAND               ( ( BaseType_t ) 6 )
#define tmrCOMMAND_START_FROM_ISR               ( ( BaseType_t ) 6 )
#define tmrCOMMAND_RESET_FROM_ISR               ( ( BaseType_t ) 7 )
#define tmrCOMMAND_STOP_FROM_ISR                ( ( BaseType_t ) 8 )
#define tmrCOMMAND_CHANGE_PERIOD_FROM_ISR       ( ( BaseType_t ) 9 )

typedef void* TimerHandle_t;

inline void * pvTimerGetTimerID( const TimerHandle_t xTimer )
{
    printf("pvTimerGetTimerID\n");
    return nullptr;
}

inline TimerHandle_t mockTimerCreateResult = nullptr;
inline TimerHandle_t xTimerCreate( const char * const pcTimerName, /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                                   const TickType_t xTimerPeriodInTicks,
                                   const BaseType_t xAutoReload,
                                   void * const pvTimerID,
                                   TimerCallbackFunction_t pxCallbackFunction )
{
    printf("xTimerCreate %s\n", pcTimerName);
    return mockTimerCreateResult;
}


inline TimerHandle_t xTimerCreateStatic( const char * const pcTimerName, /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
    const TickType_t xTimerPeriodInTicks,
    const BaseType_t xAutoReload,
    void * const pvTimerID,
    TimerCallbackFunction_t pxCallbackFunction,
    StaticTimer_t *t)
{
    printf("xTimerCreateStatic %s\n", pcTimerName);
    return nullptr;
}

inline void xTimerDelete( TimerHandle_t handle, TickType_t xTimerPeriodInTicks)
{
    printf("xTimerDelete\n");
}

inline void xTimerStop( TimerHandle_t handle, TickType_t xTimerPeriodInTicks)
{
    printf("xTimerStop\n");
}

inline BaseType_t xTimerGenericCommand( TimerHandle_t xTimer,
                                        const BaseType_t xCommandID,
                                        const TickType_t xOptionalValue,
                                        BaseType_t * const pxHigherPriorityTaskWoken,
                                        const TickType_t xTicksToWait )
{
    printf("xTimerGenericCommand\n");
    return pdPASS;
}

#define xTimerChangePeriod( xTimer, xNewPeriod, xTicksToWait ) \
    xTimerGenericCommand( ( xTimer ), tmrCOMMAND_CHANGE_PERIOD, ( xNewPeriod ), NULL, ( xTicksToWait ) )

#define xTimerStart( xTimer, xTicksToWait ) \
    xTimerGenericCommand( ( xTimer ), tmrCOMMAND_START, ( xTaskGetTickCount() ), NULL, ( xTicksToWait ) )
