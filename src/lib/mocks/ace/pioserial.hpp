#pragma once

#include "ace/constants.hpp"
#include "ace/coreutils.hpp"
#include "ace/models.hpp"

#include "etl/array_view.h"
#include "etl/delegate.h"

class PioSerial
{
public:
    using CallBackFunction = etl::delegate<void(const etl::array_view<char> &)>;

    PioSerial(const GATAS::PinTypeMap &pins, uint32_t baudrate, CallBackFunction callback)
    {
        (void)pins;
        (void)baudrate;
        (void)callback;
    }

    GATAS::PostConstruct postConstruct()
    {
        return GATAS::PostConstruct::OK;
    }

    void releaseUnstarted()
    {
    }

    void start()
    {
    }
};
