#pragma once

#include <cassert>

inline unsigned mockLwipLockDepth = 0;
inline void (*mockBeforeLwipLock)() = nullptr;

inline void cyw43_arch_lwip_begin()
{
    if (mockLwipLockDepth == 0 && mockBeforeLwipLock != nullptr)
    {
        auto callback = mockBeforeLwipLock;
        mockBeforeLwipLock = nullptr;
        callback();
    }
    ++mockLwipLockDepth;
}

inline void cyw43_arch_lwip_end()
{
    assert(mockLwipLockDepth > 0);
    --mockLwipLockDepth;
}

inline void cyw43_arch_lwip_check()
{
    assert(mockLwipLockDepth > 0);
}
