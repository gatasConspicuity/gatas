#include "../lr2021.hpp"
#include "../../lr2021driver/inc/lr20xx_hal.h"

namespace
{
RadioHardware &hardware(const void *context)
{
    return *static_cast<Lr2021 *>(const_cast<void *>(context));
}
lr20xx_hal_status_t status(bool ok)
{
    return ok ? LR20XX_HAL_STATUS_OK : LR20XX_HAL_STATUS_ERROR;
}
// Always release CS, including on a short SPI transfer.
class SelectedRadio
{
    RadioHardware &hw;
public:
    explicit SelectedRadio(RadioHardware &value) : hw(value) { hw.spi()->cs_select(hw.cs()); }
    ~SelectedRadio() { hw.spi()->cs_deselect(hw.cs()); busy_wait_us_32(1); }
};
}

lr20xx_hal_status_t lr20xx_hal_write(const void *context, const uint8_t *command, uint16_t commandLength,
                                   const uint8_t *data, uint16_t dataLength)
{
    auto &hw = hardware(context);
    if (!hw.waitReady()) { return LR20XX_HAL_STATUS_ERROR; }
    SelectedRadio selected(hw);
    return status(hw.write(command, commandLength) && hw.write(data, dataLength));
}

lr20xx_hal_status_t lr20xx_hal_read(const void *context, const uint8_t *command, uint16_t commandLength,
                                  uint8_t *data, uint16_t dataLength)
{
    auto &hw = hardware(context);
    if (lr20xx_hal_write(context, command, commandLength, nullptr, 0) != LR20XX_HAL_STATUS_OK || !hw.waitReady())
    {
        return LR20XX_HAL_STATUS_ERROR;
    }
    SelectedRadio selected(hw);
    uint8_t dummy;
    return status(hw.read(&dummy, 1) && hw.read(data, dataLength));
}

lr20xx_hal_status_t lr20xx_hal_direct_read(const void *context, uint8_t *data, uint16_t length)
{
    auto &hw = hardware(context);
    if (!hw.waitReady()) { return LR20XX_HAL_STATUS_ERROR; }
    SelectedRadio selected(hw);
    return status(hw.read(data, length));
}

lr20xx_hal_status_t lr20xx_hal_direct_read_fifo(const void *context, const uint8_t *command, uint16_t commandLength,
                                              uint8_t *data, uint16_t dataLength)
{
    auto &hw = hardware(context);
    if (!hw.waitReady()) { return LR20XX_HAL_STATUS_ERROR; }
    SelectedRadio selected(hw);
    return status(hw.write(command, commandLength) && hw.read(data, dataLength));
}

lr20xx_hal_status_t lr20xx_hal_reset(const void *context) { return status(hardware(context).reset()); }
lr20xx_hal_status_t lr20xx_hal_wakeup(const void *context) { return status(hardware(context).wakeup()); }
