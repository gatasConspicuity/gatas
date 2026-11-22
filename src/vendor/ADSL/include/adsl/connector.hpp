#pragma once

#include "etl/span.h"
#include "uplinkentry.hpp"

namespace ADSL
{

    class TrafficPayload;
    class StatusPayload;
    class Header;

    class Connector
    {

    public:
        virtual ~Connector() {}

        /**
         * @brief return a number of us. Does not matter if it's from epoch, as long as it's monotonic.
         */
        virtual uint32_t adsl_getTick() const = 0;

        /**
         * @brief send a frame to the network
         * @param ctx caller-supplied context forwarded verbatim to adsl_alloc
         * @param data the data to send
         * @return true if the frame was sent successfully
         */
        virtual bool adsl_sendFrame(const void *ctx, uint8_t const *data, size_t lengthBytes) = 0;

        // The receive context is borrowed from handleRx for the duration of the callback.
        virtual void adsl_receivedTraffic(const void *ctx, const Header &header, const ADSL::TrafficPayload &tp) = 0;
        virtual void adsl_receivedStatus(const ADSL::Header &header, const ADSL::StatusPayload &sp) = 0;

        /**
         * @brief Called once with all decoded targets in a received Traffic Uplink packet.
         * @param ctx      Caller-supplied context from handleRx.
         * @param header   The packet header (uplink sender's address / type).
         * @param entries  Span of all decoded uplink entries in this packet.
         */
        virtual void adsl_receivedUplinkTraffic(const void *ctx, const Header &header, etl::span<const ADSL::UplinkEntry> entries) = 0;

        /**
         * Request to build a status payload
         * It'a advised to override this in fill in your capabilities
         */
        virtual void adsl_buildStatusPayload(const void *ctx, ADSL::StatusPayload &sp) = 0;

        /**
         * @brief Allocate memory for the protocol layer.
         * This is used to allow the protocol layer to use the same memory pool as the rest of the application, if desired.
         */
        virtual uint8_t *adsl_alloc(const void *ctx, size_t sizeBytes) = 0;

        /**
         * @brief Acquire the protocol mutex.
         * Called by the Protocol before accessing shared state (neighbours, send functions).
         * The default no-op is suitable for single-threaded or ISR-safe implementations.
         * Override to provide real mutual exclusion when needed.
         */
        virtual void adsl_lock() {}

        /**
         * @brief Release the protocol mutex.
         * Must be paired with every adsl_lock() call.
         */
        virtual void adsl_unlock() {}
    };

}
