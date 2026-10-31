#pragma once

#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"

struct QueueDefinition; /* Using old naming convention so as not to break kernel aware debuggers. */
typedef struct QueueDefinition   * QueueHandle_t;

#define queueSEND_TO_BACK                     ( ( BaseType_t ) 0 )
#define queueSEND_TO_FRONT                    ( ( BaseType_t ) 1 )
#define queueOVERWRITE                        ( ( BaseType_t ) 2 )

/* For internal use only. */
#define queueSEND_TO_BACK                     ( ( BaseType_t ) 0 )
#define queueSEND_TO_FRONT                    ( ( BaseType_t ) 1 )
#define queueOVERWRITE                        ( ( BaseType_t ) 2 )

/* For internal use only.  These definitions *must* match those in queue.c. */
#define queueQUEUE_TYPE_BASE                  ( ( uint8_t ) 0U )
#define queueQUEUE_TYPE_SET                   ( ( uint8_t ) 0U )
#define queueQUEUE_TYPE_MUTEX                 ( ( uint8_t ) 1U )
#define queueQUEUE_TYPE_COUNTING_SEMAPHORE    ( ( uint8_t ) 2U )
#define queueQUEUE_TYPE_BINARY_SEMAPHORE      ( ( uint8_t ) 3U )
#define queueQUEUE_TYPE_RECURSIVE_MUTEX       ( ( uint8_t ) 4U )


#define xQueueSendToBack( xQueue, pvItemToQueue, xTicksToWait ) \
    xQueueGenericSend( ( xQueue ), ( pvItemToQueue ), ( xTicksToWait ), queueSEND_TO_BACK )

inline BaseType_t xQueueGenericSend( QueueHandle_t xQueue,
                                     const void * const pvItemToQueue,
                                     TickType_t xTicksToWait,
                                     const BaseType_t xCopyPosition )
{
    //printf("xQueueGenericSend\n");
    return pdPASS;
};

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
#define xQueueCreate( uxQueueLength, uxItemSize )    xQueueGenericCreate( ( uxQueueLength ), ( uxItemSize ), ( queueQUEUE_TYPE_BASE ) )

inline QueueHandle_t xQueueGenericCreate( const UBaseType_t uxQueueLength,
    const UBaseType_t uxItemSize,
    const uint8_t ucQueueType )
{
    //printf("xQueueGenericCreate\n");
    return nullptr;
}

#endif

inline BaseType_t xQueueReceive( QueueHandle_t xQueue,
                                 void * const pvBuffer,
                                 TickType_t xTicksToWait )
{
    //printf("xQueueReceive\n");
    return pdTRUE;
}

inline QueueHandle_t mockDeletedQueue = nullptr;
inline void vQueueDelete( QueueHandle_t xQueue )
{
    mockDeletedQueue = xQueue;
}

inline void vQueueAddToRegistry(QueueHandle_t xQueue, const char *pcQueueName)
{
    (void)xQueue;
    (void)pcQueueName;
}

inline BaseType_t xQueueTakeMutexRecursive( QueueHandle_t xMutex,
    TickType_t xTicksToWait )
{
    //printf("xQueueTakeMutexRecursive\n");
    return pdTRUE;
}

inline BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
    //puts("xQueueGiveMutexRecursive");
    return pdTRUE;
}
