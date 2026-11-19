#pragma once

#include "etl/vector.h"

// Deterministic desktop queue matching BTstack's deferred, coalescing callback contract.
// Tests drive delivery explicitly to exercise task interleavings without sleeps.
struct btstack_linked_item;
struct btstack_context_callback_registration_t
{
    btstack_linked_item *item = nullptr;
    void (*callback)(void *) = nullptr;
    void *context = nullptr;
};

inline etl::vector<btstack_context_callback_registration_t *, 8> mockBtstackCallbacks;
inline bool mockBtstackOnMainThread = false;

inline void btstack_run_loop_execute_on_main_thread(btstack_context_callback_registration_t *registration)
{
    for (auto *pending : mockBtstackCallbacks)
    {
        if (pending == registration)
        {
            return;
        }
    }
    mockBtstackCallbacks.push_back(registration);
}

inline void mockBtstackRunCallbacks()
{
    const bool previous = mockBtstackOnMainThread;
    mockBtstackOnMainThread = true;
    while (!mockBtstackCallbacks.empty())
    {
        auto *next = mockBtstackCallbacks.front();
        mockBtstackCallbacks.erase(mockBtstackCallbacks.begin());
        next->callback(next->context);
    }
    mockBtstackOnMainThread = previous;
}
