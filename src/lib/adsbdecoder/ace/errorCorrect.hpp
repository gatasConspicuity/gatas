#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/span.h>

#include "mode-s.hpp"

namespace ADSB
{
    constexpr size_t MAX_MODE_S_PACKET_BYTES = 14;
    constexpr size_t MAX_CORRECTABLE_BITS = 6;

    inline bool isLongModeSPacket(uint8_t downlinkFormat)
    {
        return downlinkFormat == 16 || downlinkFormat == 17 ||
               downlinkFormat == 19 || downlinkFormat == 20 ||
               downlinkFormat == 21;
    }

    inline uint32_t readParity(etl::span<const uint8_t> packet)
    {
        const size_t parityOffset = packet.size() - 3;
        return (static_cast<uint32_t>(packet[parityOffset]) << 16) |
               (static_cast<uint32_t>(packet[parityOffset + 1]) << 8) |
               packet[parityOffset + 2];
    }

    inline uint32_t packetSyndrome(etl::span<const uint8_t> packet)
    {
        return mode_s_checksum(packet.data(), static_cast<int>(packet.size() * 8)) ^ readParity(packet);
    }

    inline uint32_t bitSyndrome(uint8_t bit, int bits)
    {
        if ((bits != 56 && bits != 112) || bit >= bits)
        {
            return 0;
        }

        const int offset = (bits == 112) ? 0 : 112 - 56;
        uint32_t syndrome = mode_s_checksum_table[bit + offset];
        if (bit >= bits - 24)
        {
            syndrome ^= 1u << (bits - 1 - bit);
        }

        return syndrome;
    }

    inline bool isSupportedFrame(etl::span<const uint8_t> packet)
    {
        const uint8_t downlinkFormat = packet[0] >> 3;
        return (downlinkFormat == 11 && packet.size() == 7) ||
               (downlinkFormat == 17 && packet.size() == MAX_MODE_S_PACKET_BYTES);
    }

    // Corrects only bits flagged as Manchester decoding errors. Returns the
    // number of flipped bits, zero for an already-valid packet, or -1 if no
    // checksum-valid candidate was found.
    inline int Correct(etl::span<uint8_t> packet, etl::span<const uint8_t> packetErrors)
    {
        if ((packet.size() != 7 && packet.size() != MAX_MODE_S_PACKET_BYTES) ||
            packetErrors.size() != packet.size())
        {
            return -1;
        }

        uint32_t syndrome = packetSyndrome(packet);
        if (syndrome == 0 && isSupportedFrame(packet))
        {
            return 0;
        }

        uint8_t errorBitIndices[MAX_CORRECTABLE_BITS]{};
        size_t errorBitCount = 0;
        for (size_t byteIndex = 0; byteIndex < packetErrors.size(); ++byteIndex)
        {
            for (uint8_t bitIndex = 0; bitIndex < 8; ++bitIndex)
            {
                const uint8_t mask = static_cast<uint8_t>(0x80u >> bitIndex);
                if ((packetErrors[byteIndex] & mask) == 0)
                {
                    continue;
                }

                if (errorBitCount == MAX_CORRECTABLE_BITS)
                {
                    return -1;
                }

                errorBitIndices[errorBitCount++] = static_cast<uint8_t>(byteIndex * 8 + bitIndex);
            }
        }

        if (errorBitCount == 0)
        {
            return -1;
        }

        uint32_t bitSyndromes[MAX_CORRECTABLE_BITS]{};
        const int packetBits = static_cast<int>(packet.size() * 8);
        for (size_t i = 0; i < errorBitCount; ++i)
        {
            bitSyndromes[i] = bitSyndrome(errorBitIndices[i], packetBits);
        }

        const uint32_t candidateCount = 1u << errorBitCount;
        uint32_t previousGrayCode = 0;
        for (uint32_t candidateIndex = 1; candidateIndex < candidateCount; ++candidateIndex)
        {
            const uint32_t grayCode = candidateIndex ^ (candidateIndex >> 1);
            uint32_t changedBit = 0;
            while (((grayCode ^ previousGrayCode) & (1u << changedBit)) == 0)
            {
                ++changedBit;
            }

            syndrome ^= bitSyndromes[changedBit];
            if (syndrome == 0)
            {
                uint8_t candidate[MAX_MODE_S_PACKET_BYTES]{};
                for (size_t i = 0; i < packet.size(); ++i)
                {
                    candidate[i] = packet[i];
                }

                uint32_t bits = grayCode;
                int correctedBits = 0;
                size_t errorIndex = 0;
                while (bits != 0)
                {
                    if ((bits & 1u) != 0)
                    {
                        const uint8_t bitIndex = errorBitIndices[errorIndex];
                        candidate[bitIndex >> 3] ^= static_cast<uint8_t>(0x80u >> (bitIndex & 7));
                    }
                    correctedBits += static_cast<int>(bits & 1u);
                    bits >>= 1;
                    ++errorIndex;
                }

                if (isSupportedFrame(etl::span<const uint8_t>(candidate, packet.size())))
                {
                    for (size_t i = 0; i < packet.size(); ++i)
                    {
                        packet[i] = candidate[i];
                    }
                    return correctedBits;
                }
            }

            previousGrayCode = grayCode;
        }

        return -1;
    }
}
