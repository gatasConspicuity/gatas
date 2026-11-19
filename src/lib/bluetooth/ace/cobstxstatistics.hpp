#pragma once

#include <stdint.h>
#include "etl/span.h"

// Counts local ATT acceptance, not delivery acknowledgements from the peer.
struct CobsTxStatistics
{
    uint32_t packets = 0;
    uint32_t bytes = 0;
    uint32_t notifications = 0;
    uint32_t errors = 0;

    void record(etl::span<const uint8_t> payload, bool accepted)
    {
        if (!accepted)
        {
            ++errors;
            return;
        }
        ++notifications;
        bytes += payload.size();
        // A COBS packet may span notifications or share one with other packets.
        // Only its terminating zero completes the packet on this connection.
        for (const auto byte : payload)
        {
            if (byte == 0)
            {
                ++packets;
            }
        }
    }
};
