#include <stdint.h>
#include <stdio.h>
#include "../flashstore.hpp"

#include "pico/flash.h"
#include "FreeRTOS.h"
#include "task.h"

extern char __flash_binary_end;

typedef struct
{
    const size_t size;
    uint32_t address;
    const uint8_t *p1;
} FlashMutation;

FlashStore::FlashStore(size_t size_, size_t startsOffsetFromEnd_) : ConfigStore(),
                                                                    _size(((size_ + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE),
                                                                    startsOffsetFromEnd(((startsOffsetFromEnd_ + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE),
                                                                    bytesWritten(0)
{
    // #if defined(RUN_FREERTOS_ON_CORE)
    //     flash_safe_execute_core_init();
    // #endif
}

uint32_t FlashStore::flashAddress() const
{
    return (PICO_FLASH_SIZE_BYTES - startsOffsetFromEnd);
}

void FlashStore::rewind()
{
    // NOOP
}

size_t FlashStore::write(uint8_t c)
{
    (void)c;
    panic("Operation of one byte not supported in FlashStore");
}

void __not_in_flash_func(pico_flash_bank_perform_flash)(void *param)
{
    const FlashMutation *mop = (const FlashMutation *)param;
    auto flashEraseBytes = ((mop->size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    flash_range_erase(mop->address, flashEraseBytes);

    if (mop->p1 != nullptr)
    {
        // Known limitation: flash_range_program() requires whole pages, so the
        // final page may read beyond the logical payload. Current callers use
        // fixed-size backing stores with enough remaining storage for this.
        auto flashProgramBytes = ((mop->size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
        flash_range_program(mop->address, mop->p1, flashProgramBytes);
    }
}

size_t __not_in_flash_func(FlashStore::write)(const uint8_t *buffer, size_t length)
{
    if (length > _size)
    {
        return 0;
    }

    FlashMutation mop =
        {
            .size = length,
            .address = flashAddress(),
            .p1 = buffer};

    // Check if FreeRTOS scheduler is running
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        // Safe version when multitasking
        if (flash_safe_execute(pico_flash_bank_perform_flash, &mop, UINT32_MAX) == PICO_OK)
        {
            bytesWritten = length;
            return length;
        }
    }
    else
    {
        puts("Doing Flash operations seems to be buggy outside of FreeRTOS task, please call these only from within a FreeRTOS Task");
    }
    return 0;
}

size_t __not_in_flash_func(FlashStore::erase)()
{
    return write(nullptr, _size);
}

const uint8_t *FlashStore::data() const
{
    return (const uint8_t *)(XIP_BASE + flashAddress());
}

size_t FlashStore::writtenSize() const
{
    return bytesWritten;
}

size_t FlashStore::capacity() const
{
    return _size;
}
