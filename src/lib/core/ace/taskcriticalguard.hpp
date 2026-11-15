#pragma once

#include <stdint.h>
#include <type_traits>
#include <utility>
#include "FreeRTOS.h"
#include "task.h"
#include "etl/utility.h"

/**
 * @brief Guard based on FreeRTOS critical section (task-only)
 * Use case: very short critical sections (e.g., copy small structs).
 * This is a global critical section per core, not a per-lock spinlock.
 */
class TaskCriticalGuard
{
private:
    bool entered;

public:
    explicit TaskCriticalGuard() : entered(true)
    {
        taskENTER_CRITICAL();
    }

    ~TaskCriticalGuard()
    {
        if (entered)
        {
            taskEXIT_CRITICAL();
        }
    }

    TaskCriticalGuard(const TaskCriticalGuard &) = delete;
    TaskCriticalGuard &operator=(const TaskCriticalGuard &) = delete;
    TaskCriticalGuard(TaskCriticalGuard &&) = delete;
    TaskCriticalGuard &operator=(TaskCriticalGuard &&) = delete;

    // These function are dangerious if not used correctly, do not re-enable
    // template <typename T>
    // inline static T copyWithLock(const T &value)
    // {
    //     TaskCriticalGuard guard{};
    //     return value;
    // }

    // template <typename T1, typename T2>
    // inline static etl::pair<T1, T2> copyWithLock(const T1 &val1, const T2 &val2)
    // {
    //     TaskCriticalGuard guard{};
    //     return etl::pair<T1, T2>(val1, val2);
    // }

    operator bool() const
    {
        return true;
    }
};

