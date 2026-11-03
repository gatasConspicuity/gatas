#pragma once

#include <stdint.h>

namespace GATAS
{
    enum class DeviceAction : uint8_t { Status = 0, Save = 1, Restart = 2, UsbBoot = 3 };

    struct DeviceActionRequest
    {
        DeviceAction action;
        uint8_t bootId;
    };
}
