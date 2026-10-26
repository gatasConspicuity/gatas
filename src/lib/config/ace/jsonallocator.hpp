#pragma once

#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

#include "FreeRTOS.h"
#include "ArduinoJson.hpp"
#include "ace/debug.hpp"

class FreeRTOSJsonAllocator : public ArduinoJson::Allocator
{
    struct alignas(std::max_align_t) AllocationHeader
    {
        size_t size;
    };

public:
    void *allocate(size_t size) override
    {
        if (size == 0 || size > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader))
        {
            return nullptr;
        }
        auto *memory = pvPortMalloc(sizeof(AllocationHeader) + size);
        if (memory == nullptr)
        {
            GATAS_WARN("FreeRTOSJsonAllocator: failed to allocate %zu bytes", size);
            return nullptr;
        }
        auto *header = new (memory) AllocationHeader{size};
        return header + 1;
    }

    void deallocate(void *ptr) override
    {
        if (ptr != nullptr)
        {
            vPortFree(static_cast<AllocationHeader *>(ptr) - 1);
        }
    }

    void *reallocate(void *ptr, size_t size) override
    {
        if (ptr == nullptr)
        {
            return allocate(size);
        }
        if (size == 0)
        {
            deallocate(ptr);
            return nullptr;
        }
        auto *header = static_cast<AllocationHeader *>(ptr) - 1;
        if (size <= header->size)
        {
            return ptr;
        }
        auto *replacement = allocate(size);
        if (replacement != nullptr)
        {
            std::memcpy(replacement, ptr, header->size);
            deallocate(ptr);
        }
        return replacement;
    }
};
