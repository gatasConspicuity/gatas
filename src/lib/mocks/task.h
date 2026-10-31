#pragma once

#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"

inline void portYIELD_FROM_ISR(BaseType_t xHigherPriorityTaskWoken)
{
    printf("portYIELD_FROM_ISR\n");
}

inline void vTaskDelay(uint32_t delayMs)
{
    printf("vTaskDelay %d ms\n", delayMs);
}

inline TaskHandle_t mockDeletedTask = nullptr;
inline void vTaskDelete(TaskHandle_t pxTaskCode)
{
    mockDeletedTask = pxTaskCode;
    printf("vTaskDelete\n");
}

inline BaseType_t mockTaskCreateResult = pdPASS;
inline TaskHandle_t mockCreatedTask = nullptr;
inline BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                              const char *const pcName, /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                              const configSTACK_DEPTH_TYPE usStackDepth,
                              void *const pvParameters,
                              UBaseType_t uxPriority,
                              TaskHandle_t *const pxCreatedTask)
{
    printf("xTaskCreate %s\n", pcName);
    if (mockTaskCreateResult == pdPASS && pxCreatedTask != nullptr)
    {
        *pxCreatedTask = mockCreatedTask;
    }
    return mockTaskCreateResult;
}

inline TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                                      const char *const pcName, /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                                      const configSTACK_DEPTH_TYPE usStackDepth,
                                      void *const pvParameters,
                                      UBaseType_t uxPriority,
                                      StackType_t *st, StaticTask_t *b)
{
    printf("xTaskCreateStatic %s\n", pcName);
    return pdPASS;
}

inline BaseType_t xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify,
    UBaseType_t uxIndexToNotify,
    uint32_t ulValue,
    eNotifyAction eAction,
    uint32_t *pulPreviousNotificationValue,
    BaseType_t *pxHigherPriorityTaskWoken)
{
    printf("xTaskGenericNotifyFromISR\n");
    return 0;
}

inline BaseType_t xTaskGenericNotify(TaskHandle_t xTaskToNotify,
                                     UBaseType_t uxIndexToNotify,
                                     UBaseType_t ulValue,
                                     eNotifyAction eAction,
                                     uint32_t *pulPreviousNotificationValue)
{
    printf("xTaskGenericNotify\n");
    return 0;
}

#define xTaskNotifyFromISR(xTaskToNotify, ulValue, eAction, pxHigherPriorityTaskWoken) \
    xTaskGenericNotifyFromISR((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), (eAction), NULL, (pxHigherPriorityTaskWoken))
// #define xTaskNotifyIndexedFromISR( xTaskToNotify, uxIndexToNotify, ulValue, eAction, pxHigherPriorityTaskWoken ) \
//     xTaskGenericNotifyFromISR( ( xTaskToNotify ), ( uxIndexToNotify ), ( ulValue ), ( eAction ), NULL, ( pxHigherPriorityTaskWoken ) )

inline UBaseType_t xTaskNotifyulValue = 0;
inline bool xTaskNotifyCalled = false;
inline void xTaskNotify(TaskHandle_t xTaskToNotify, UBaseType_t ulValue, eNotifyAction eAction)
{
    xTaskNotifyulValue = ulValue;
    xTaskNotifyCalled = true;
}

inline uint32_t ulTaskGenericNotifyTake(UBaseType_t uxIndexToWaitOn,
                                        BaseType_t xClearCountOnExit,
                                        TickType_t xTicksToWait)
{
    printf("ulTaskGenericNotifyTake\n");
    return pdPASS;
};
#define ulTaskNotifyTake(xClearCountOnExit, xTicksToWait) \
    ulTaskGenericNotifyTake((tskDEFAULT_INDEX_TO_NOTIFY), (xClearCountOnExit), (xTicksToWait))
#define ulTaskNotifyTakeIndexed(uxIndexToWaitOn, xClearCountOnExit, xTicksToWait) \
    ulTaskGenericNotifyTake((uxIndexToWaitOn), (xClearCountOnExit), (xTicksToWait))

inline BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                                  uint64_t ulBitsToClearOnExit,
                                  uint32_t *pulNotificationValue,
                                  TickType_t xTicksToWait)
{
    (void)ulBitsToClearOnEntry;
    (void)ulBitsToClearOnExit;
    (void)xTicksToWait;
    if (pulNotificationValue != nullptr)
    {
        *pulNotificationValue = xTaskNotifyCalled ? xTaskNotifyulValue : 0;
    }
    printf("xTaskNotifyWait\n");
    return xTaskNotifyCalled ? pdPASS : pdFAIL;
}

inline TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    printf("xTaskGetCurrentTaskHandle\n");
    return nullptr;
}

inline TickType_t xTaskGetTickCount(void)
{
    printf("xTaskGetTickCount\n");
    return 0;
}

#define eDeleted (0)
inline uint32_t eTaskGetState(TaskHandle_t xTask)
{
    printf("xTaskGenericNotify\n");
    return 0;
}
