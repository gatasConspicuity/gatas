#pragma once

#include <stdint.h>
#include "debug.hpp"
#include "pico/sync.h"
#include "etl/utility.h"

/**
 * @brief Classic Guard based on spinlock
 * Usecase if you now the lock will be very short, like copying of data from configurations changes
 * Don't use this for resources, lock is fast, but should happen only for microseconds
 * Additional note: Since these spinlocks disable interrups, they are assumed to be safe with FreeRTOS
 */
class SpinlockGuard
{
private:
    spin_lock_t *lock;
    uint32_t save;

public:
    explicit SpinlockGuard(spin_lock_t *lock)
        : lock(lock), save(spin_lock_blocking(lock)) {}

    ~SpinlockGuard()
    {
        spin_unlock(lock, save);
    }

    static spin_lock_t *claim()
    {
        return spin_lock_instance((uint)spin_lock_claim_unused(true));
    }

    SpinlockGuard(const SpinlockGuard &) = delete;
    SpinlockGuard &operator=(const SpinlockGuard &) = delete;
    SpinlockGuard(SpinlockGuard &&) = delete;
    SpinlockGuard &operator=(SpinlockGuard &&) = delete;

    template <typename T>
    inline static auto copyWithLock(spin_lock_t *lock, const T &value)
    {
        SpinlockGuard guard(lock);
        return value;
    }

    // template <typename F>
    // inline static auto withLock(spin_lock_t *lock, F &&fn)
    // {
    //     SpinlockGuard guard(lock);
    //     return etl::forward<F>(fn)();
    // }

    template <typename T1, typename T2>
    inline static auto copyWithLock(spin_lock_t *lock, const T1 &val1, const T2 &val2)
    {
        SpinlockGuard guard(lock);
        return etl::pair<T1, T2>(val1, val2);
    }

    operator bool() const
    {
        return true;
    }
};

/**
 * @brief A value with spinlock-protected snapshot reads and writes.
 * The supplied lock must be initialized and outlive this object.
 * Default-constructed objects require init() before load() or store().
 * Use only values whose copies and assignments are short and non-blocking.
 * A load followed by a store is not an atomic read-modify-write operation.
 */
template <typename T>
class SynchronizedValue
{
private:
    spin_lock_t *lock = nullptr;
    T value{};

public:
    SynchronizedValue() = default;

    explicit SynchronizedValue(spin_lock_t *lock, const T &initialValue = T{})
        : lock(lock), value(initialValue)
    {
        GATAS_ASSERT(lock != nullptr, "SynchronizedValue requires a valid spinlock");
    }

    SynchronizedValue(const SynchronizedValue &) = delete;
    SynchronizedValue &operator=(const SynchronizedValue &) = delete;
    SynchronizedValue(SynchronizedValue &&) = delete;
    SynchronizedValue &operator=(SynchronizedValue &&) = delete;

    /** Initialize once during startup, before any concurrent access. */
    void init(spin_lock_t *newLock, const T &initialValue = T{})
    {
        GATAS_ASSERT(lock == nullptr, "SynchronizedValue already initialized");
        GATAS_ASSERT(newLock != nullptr, "SynchronizedValue requires a valid spinlock");
        value = initialValue;
        lock = newLock;
    }

    T load() const
    {
        GATAS_ASSERT(lock != nullptr, "SynchronizedValue must be initialized before load");
        SpinlockGuard guard(lock);
        return value;
    }

    void store(const T &newValue)
    {
        GATAS_ASSERT(lock != nullptr, "SynchronizedValue must be initialized before store");
        SpinlockGuard guard(lock);
        value = newValue;
    }

    /** Modify in place under the lock. The callback must be short and must not
     * acquire locks, block, or retain references to the protected value. */
    template <typename F>
    void update(F &&fn)
    {
        GATAS_ASSERT(lock != nullptr, "SynchronizedValue must be initialized before update");
        SpinlockGuard guard(lock);
        etl::forward<F>(fn)(value);
    }
};
