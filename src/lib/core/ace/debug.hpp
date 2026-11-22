#pragma once

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

// 0;30m black      1;30m bold black      0;90m light black
// 0;31m red        1;31m bold red        0;91m light red
// 0;32m green      1;32m bold green      0;92m light green
// 0;33m yellow     1;33m bold yellow     0;93m light yellow
// 0;34m blue       1;34m bold blue       0;94m light blue
// 0;35m magenta    1;35m bold magenta    0;95m light magenta
// 0;36m cyan       1;36m bold cyan       0;96m light cyan
// 0;37m white      1;37m bold white      0;97m light white

#if GATAS_DEBUG == 1

#include <stdio.h>
#include "pico.h"
#include "task.h"

static constexpr const char *basename(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

inline void gatasRegisterMutex(SemaphoreHandle_t mutex, const char *name)
{
    if (mutex != nullptr)
    {
        vQueueAddToRegistry(reinterpret_cast<QueueHandle_t>(mutex), name);
    }
}

#define GATAS_INFO(fmt, ...) \
    printf("\033[01;32m(%s:%d) INFO: " fmt "\e[0m\n", basename(__FILE__), __LINE__, ##__VA_ARGS__)

#define GATAS_WARN(fmt, ...) \
    printf("\033[01;33m(%s:%d) WARN: " fmt "\e[0m\n", basename(__FILE__), __LINE__, ##__VA_ARGS__)

#define GATAS_LOG_IF(mask, fmt, ...)                                  \
    do                                                                \
    {                                                                 \
        if (GATAS_LOG_ACTIVE_MODULES & (mask))                        \
            printf("\033[01;32m(%s:%d) INFO: " fmt "\e[0m\n",        \
                   basename(__FILE__), __LINE__, ##__VA_ARGS__);      \
    } while (0)

#define GATAS_ASSERT(cond, fmt, ...)                                  \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
        {                                                             \
            printf("\033[01;31m(%s:%d) ASSERT: " fmt "\e[0m\n",      \
                   basename(__FILE__), __LINE__, ##__VA_ARGS__);      \
            vTaskDelay(TASK_DELAY_MS(10));                            \
            panic("Assertion");                                       \
        }                                                             \
    } while (0)

#define GATAS_VERIFY(cond, msg)                                       \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
            printf("\033[33m VERIFY: %s (%s:%d)\e[0m\n",              \
                   msg, basename(__FILE__), __LINE__);                \
    } while (0)

#define GATAS_REGISTER_MUTEX(mutex, name) gatasRegisterMutex((mutex), (name))

#else

#define GATAS_INFO(...)        ((void)0)
#define GATAS_WARN(...)        ((void)0)
#define GATAS_ASSERT(...)      ((void)0)
#define GATAS_VERIFY(...)      ((void)0)
#define GATAS_LOG_IF(...)      ((void)0)
#define GATAS_REGISTER_MUTEX(...) ((void)0)

#endif
