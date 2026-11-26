#pragma once

#include <stdint.h>

#include <etl/array.h>
#include <etl/span.h>

#include "ace/basemodule.hpp"

class CountryRegulations
{
public:
    static constexpr uint8_t MAX_COMBINED_TIMINGS = 4;

    struct Zone
    {
        enum enum_type : uint8_t
        {
            ZONE0, // Zone is unknown and no transmission will take place
            ZONE1, // Zone 1: Europe, Africa, Russia, China (30W to 110E, excl. zone 5) All protocols MUST be part of ZONE1. see : RadioTuner::addConspicuityProtocolToTasks()
            ZONE2, // Zone 2: North America (west of 30W, north of 10N)
            ZONE3, // Zone 3: New Zealand (east of 160E)
            ZONE4, // Zone 4: Australia (110E to 160E)
            ZONE5, // Zone 5: Israel (34E to 54E and 29.25N to 33.5N
            ZONE6  // Zone 6: South America (west of 30W, south of 10N)
        };

        ETL_DECLARE_ENUM_TYPE(Zone, uint8_t)
        ETL_ENUM_TYPE(ZONE0, "ZONE0")
        ETL_ENUM_TYPE(ZONE1, "ZONE1")
        ETL_ENUM_TYPE(ZONE2, "ZONE2")
        ETL_ENUM_TYPE(ZONE3, "ZONE3")
        ETL_ENUM_TYPE(ZONE4, "ZONE4")
        ETL_ENUM_TYPE(ZONE5, "ZONE5")
        ETL_ENUM_TYPE(ZONE6, "ZONE6")
        ETL_END_ENUM_TYPE
    };

    enum class Channel : uint8_t
    {
        // Europe has 2 channels channel 0 = 868.2MHz, channel 1 = 868.4MHz. Frequency is finaly decided by the frequency table per area
        NOOP,
        CH00,        // Channel 0 base Frequency
        CH01,        // Channel 1 base Frequency + separation
        CH00_01,     // Random pick channel 0 or 1
        CH00_01_SEC, // when second is even channel 0  when second isodd channel 1
        CH24,        // Channel by epoch based on 24 channels (see frequencyByTimestamp)
        CH65         // Channel by epoch based on 64 channels (see frequencyByTimestamp)
    };

    struct ChannelTiming
    {
        // Remove 10ms from the end time so TX timings will not (eadsely) overflow into a region that won't be received
        static constexpr uint16_t REDUCE_ENDTIME_MS = 10;
        Channel channel = Channel::NOOP;
        // Start / end tells between which ms in a second the protocol is active
        uint16_t start = 0;
        uint16_t end = 0;
        uint8_t id = 0;

        constexpr bool operator==(const ChannelTiming &o) const
        {
            return channel == o.channel && start == o.start && end == o.end && id == o.id;
        }
    };

    struct ProtocolRxTimeSlot
    {
        CountryRegulations::Zone::enum_type zone;
        GATAS::RfConfig rfConfig;
        GATAS::LinkLayerConfig radioConfig;
        etl::span<const ChannelTiming> timeSlots;
    };

    struct ProtocolTxTimeSlot : public ProtocolRxTimeSlot
    {
        // txMinTime and txMaxTime tells how often a transmission needs to happen, used when GATAS is moving
        uint16_t txMinTime;
        uint16_t txMaxTime;
        // When GATAS is not moving, use these timings
        uint16_t reducedTxMinTime;
        uint16_t reducedTxMaxTime;
        uint8_t waitAfterCatStart;
        uint8_t waitAfterCatEnd;
    };

    // clang-format off
    static constexpr GATAS::RfConfig Europe_m     {GATAS::Modulation::GFSK,   868'200'000, 200'000, 14, 234300, 100000, 50000,  5, GATAS::RadioCapability::Gfsk868}; // 8868.200 / 868.400
    static constexpr GATAS::RfConfig Europe_hdr   {GATAS::Modulation::GFSK,   869'525'000, 200'000, 27, 234300, 200000, 50000,  5, GATAS::RadioCapability::Gfsk868}; // 869.525
    static constexpr GATAS::RfConfig Europe_ldr   {GATAS::Modulation::GFSK,   869'525'000, 200'000, 27, 234300,  38400, 12500, 10, GATAS::RadioCapability::Gfsk868}; // 869.525
    static constexpr GATAS::RfConfig Europe_lora  {GATAS::Modulation::LORA,   868'200'000, 200'000, 22, 250000,      0,     0,  0, GATAS::RadioCapability::Lora868}; // 868.200 / 868.400
    static constexpr GATAS::RfConfig Adsb1090     {GATAS::Modulation::OOK,  1'090'000'000,        0, 0, 3'076'000, 2'000'000, 0, 0, GATAS::RadioCapability::Ook1090};

    // Not validated
    static constexpr GATAS::RfConfig NorthAmerica {GATAS::Modulation::GFSK, 902'200'000, 400'000, 30, 234300, 100000, 50000, 5, GATAS::RadioCapability::Gfsk915};
    static constexpr GATAS::RfConfig NewZealand   {GATAS::Modulation::GFSK, 869'250'000, 200'000, 10, 234300, 100000, 50000, 5, GATAS::RadioCapability::Gfsk868};
    static constexpr GATAS::RfConfig Australia    {GATAS::Modulation::GFSK, 917'000'000, 400'000, 30, 234300, 100000, 50000, 5, GATAS::RadioCapability::Gfsk915};
    static constexpr GATAS::RfConfig Israel       {GATAS::Modulation::GFSK, 916'200'000, 200'000, 22, 234300, 100000, 50000, 5, GATAS::RadioCapability::Gfsk915};
    static constexpr GATAS::RfConfig SouthAmerica {GATAS::Modulation::GFSK, 917'000'000, 400'000, 30, 234300, 100000, 50000, 5, GATAS::RadioCapability::Gfsk915};

    // First byte of the syncWord is the preamble for TX
    //                                                                  protocol                             packetLength when 0 enables variable packet length mode, acts as a max packet length in variable mode
    //                                                                                                            txPreambleLength
    //                                                                                                                syncLength SYNC;
    //                                                                                                                    skip sync bits in RX mode
    static constexpr GATAS::LinkLayerConfig PROTOCOL_NONE          { 0, GATAS::ConspicuityProtocol::NONE,       true,   0, 16, 64, 0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};       // NONE
    static constexpr GATAS::LinkLayerConfig PROTOCOL_FLARM         { 2, GATAS::ConspicuityProtocol::FLARM,      true,  26, 16, 64, 8, {0x55, 0x99, 0xA5, 0xA9, 0x55, 0x66, 0x65, 0x96}};       // FLARM 0 airtime 6ms
    static constexpr GATAS::LinkLayerConfig PROTOCOL_OGN           { 3, GATAS::ConspicuityProtocol::OGN,       true,  26, 16, 64, 8, {0xAA, 0x66, 0x55, 0xA5, 0x96, 0x99, 0x96, 0x5A}};       // OGN 1 airtime 6ms <- This seems to be in use 20 Byte packet length :: 6 byte CRC

    static constexpr GATAS::LinkLayerConfig PROTOCOL_ADSL          { 4, GATAS::ConspicuityProtocol::ADSL_M_BAND,      true,  25, 16, 48, 8, {0x55, 0x99, 0x95, 0xA6, 0x9A, 0x65}};                   // ADSL on normal 0xA9, 0x6A => 0x18 26Byte first byt elength = 25Byte
    static constexpr GATAS::LinkLayerConfig PROTOCOL_ADSLO_HDR     { 5, GATAS::ConspicuityProtocol::ADSL_O_BAND_HDR, false,   0, 16, 16, 0, {0x2D, 0xD4}};                                           // ADSL on O band HDR
    static constexpr GATAS::LinkLayerConfig PROTOCOL_FANET         { 7, GATAS::ConspicuityProtocol::FANET,     false, 200, 12,  8, 0, {0xF1}};                                                 // FANET
    // Mode S downlink formats have different first bits, so do not hardware-filter on a fixed sync word.
    static constexpr GATAS::LinkLayerConfig PROTOCOL_ADSB          {10, GATAS::ConspicuityProtocol::ADSB,      true,  14, 0, 0, 0, {0}, GATAS::CombinedRadioRx::None, true};

    // Needs further research I did not get reliable reception yet
    static constexpr GATAS::LinkLayerConfig PROTOCOL_RX_ADSLFLARM  { 8, GATAS::ConspicuityProtocol::NONE,  true,  29, 16, 16, 0, {0x96, 0xA5}, GATAS::CombinedRadioRx::AdslFlarm};     // works to receive FLARM                                     // ADSL/FLARM RX Sync
    static constexpr GATAS::LinkLayerConfig PROTOCOL_RX_ADSLOGN    { 9, GATAS::ConspicuityProtocol::NONE,    true,  29, 16, 16, 0, {0x99, 0x95}, GATAS::CombinedRadioRx::AdslOgn};     // ADSL/OGN RX SYNC
    // Needs further research I did not get reliable reception yet

    // Europe
    static constexpr etl::array<ChannelTiming, 1> NOOP {ChannelTiming{Channel::CH00,      0, 1000, 0}};
    static constexpr ProtocolRxTimeSlot ADSB_RX_TIMESLOT{CountryRegulations::Zone::enum_type::ZONE0, Adsb1090, PROTOCOL_ADSB, etl::span(NOOP)};
    static constexpr etl::array EU_FLARMT              {ChannelTiming{Channel::CH01,      0,  200, 0}, ChannelTiming{Channel::CH00, 400,  800, 0}, ChannelTiming{Channel::CH01, 800, 1000, 0}};
    static constexpr etl::array EU_OGNT                {ChannelTiming{Channel::CH00,      0,  200, 0}, ChannelTiming{Channel::CH01, 400,  800, 0}, ChannelTiming{Channel::CH00, 800, 1000, 0}};
    static constexpr etl::array EU_ADSLM               {ChannelTiming{Channel::CH00,    450,  800, 0}, ChannelTiming{Channel::CH01, 800, 1000, 0}};
    static constexpr etl::array EU_ADSLO_HDR           {ChannelTiming{Channel::CH00,    200, 1000, 0}};
    static constexpr etl::array EU_ADSLO_HDRT_TRAFFIC  {ChannelTiming{Channel::CH00,    450, 1000, 0}};
    static constexpr etl::array EU_ADSLO_HDRT_UPLINK   {ChannelTiming{Channel::CH00,    200,  450, 1}}; // id=1 indicates Uplink for ADSL
    static constexpr etl::array EU_FANETT              {ChannelTiming{Channel::CH00,      0, 1000, 0}};

    // North America
    static constexpr etl::array NA_FLARMT              {ChannelTiming{Channel::CH65, 0, 1000, 0}};
    static constexpr etl::array NA_OGNT                {ChannelTiming{Channel::CH65, 0, 1000, 0}};
    static constexpr etl::array NA_ADSL                {ChannelTiming{Channel::CH65, 0, 1000, 0}};

    // static constexpr etl::array EU_ADSLFLARMT          {ChannelTiming{Channel::CH00,    400,  800, 0}, ChannelTiming{Channel::CH01, 800, 1200, 0}};
    // static constexpr etl::array EU_ADSLOGNT            {ChannelTiming{Channel::CH01,    400,  800, 0}, ChannelTiming{Channel::CH00, 800, 1200, 0}};

    static constexpr ProtocolTxTimeSlot NOOP_TX_TIMESLOT{ CountryRegulations::Zone::enum_type::ZONE0, Europe_m,       PROTOCOL_NONE,  etl::span(NOOP), 0, 0, 0, 0, 0, 0};
    static constexpr ProtocolRxTimeSlot NOOP_RX_TIMESLOT{ CountryRegulations::Zone::enum_type::ZONE0, Europe_m,       PROTOCOL_NONE,  etl::span(NOOP)};

    /**
     * When adding additional zones, all zones must be grpouped together
     */
    static constexpr etl::array protocolRxTimimgs {
        // Needs further research
        // ProtocolRxTimeSlot{ CountryRegulations::Zone::ZONE1, Europe,       PROTOCOL_RX_ADSLOGN,    etl::span(EU_ADSLOGNT)},
        // ProtocolRxTimeSlot{ CountryRegulations::Zone::ZONE1, Europe,       PROTOCOL_RX_ADSLFLARM,  etl::span(EU_ADSLFLARMT)},
        // Needs further research

        ProtocolRxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_m,       PROTOCOL_FLARM,     etl::span(EU_FLARMT)},
        ProtocolRxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_hdr,     PROTOCOL_ADSLO_HDR, etl::span(EU_ADSLO_HDR)},
        ProtocolRxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_m,       PROTOCOL_ADSL,      etl::span(EU_ADSLM)},
        ProtocolRxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_m,       PROTOCOL_OGN,       etl::span(EU_OGNT)},
        ProtocolRxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_lora,    PROTOCOL_FANET,     etl::span(EU_FANETT)}
    };

    /**
     * When adding additional zones, all zones must be grpouped together
     */
    static constexpr auto protocolTxTimimgs = etl::make_array<const ProtocolTxTimeSlot>(
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_hdr,   PROTOCOL_ADSLO_HDR,     etl::span(EU_ADSLO_HDRT_TRAFFIC), 600, 1400, 5000, 6000, 15, 250},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_hdr,   PROTOCOL_ADSLO_HDR,     etl::span(EU_ADSLO_HDRT_UPLINK),  800, 1600,  800, 1600, 15, 250},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_m,     PROTOCOL_ADSL,          etl::span(EU_ADSLM),              600, 1400, 5000, 6000, 15, 250},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_m,     PROTOCOL_OGN,           etl::span(EU_OGNT),               600, 1400, 5000, 6000, 15, 150},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_m,     PROTOCOL_FLARM,         etl::span(EU_FLARMT),             600, 1400, 5000, 6000, 15, 150},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE1, Europe_lora,  PROTOCOL_FANET,         etl::span(EU_FANETT),            2000, 3000, 6000, 7000, 15, 000},

        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE2, NorthAmerica, PROTOCOL_FLARM,         etl::span(NA_FLARMT),             600, 1400, 5000, 6000, 15, 150},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE2, NorthAmerica, PROTOCOL_OGN,           etl::span(NA_OGNT),               600, 1400, 5000, 6000, 15, 150},
        ProtocolTxTimeSlot{ CountryRegulations::Zone::enum_type::ZONE2, NorthAmerica, PROTOCOL_ADSL,          etl::span(NA_ADSL),               600, 1400, 5000, 6000, 15, 150}
    );
    static constexpr size_t MAX_PROTOCOL_TX_TIMINGS = []() {
        size_t maximum = 0;
        for (const auto &candidate : protocolTxTimimgs)
        {
            size_t count = 0;
            for (const auto &slot : protocolTxTimimgs)
            {
                if (slot.zone == candidate.zone)
                {
                    ++count;
                }
            }
            maximum = count > maximum ? count : maximum;
        }
        return maximum;
    }();
    // clang-format on

    static constexpr uint8_t validateProtocolTxTimings()
    {
        for (auto &pts : protocolTxTimimgs)
        {
            // 2) txMin <= txMax
            if (pts.txMinTime > pts.txMaxTime)
            {
                return 2;
            }

            if (pts.reducedTxMinTime < pts.txMinTime)
            {
                return 20;
            }

            if (pts.reducedTxMaxTime < pts.txMaxTime)
            {
                return 21;
            }

            if (pts.reducedTxMinTime > pts.reducedTxMaxTime)
            {
                return 18;
            }

            // 3) timing span must not be empty except PROTOCOL_NONE
            if (pts.radioConfig.protocol() != GATAS::ConspicuityProtocol::NONE && pts.timeSlots.size() == 0)
            {
                return 3;
            }

            if (pts.zone == CountryRegulations::Zone::ZONE0)
            {
                return 7;
            }

            if (GATAS::radioProtocolIndex(pts.radioConfig.protocol()) >= GATAS::RADIO_PROTOCOLS.size())
            {
                return 4;
            }

            // 4) Validate each ChannelTiming
            for (auto &t : pts.timeSlots)
            {
                if (t.start == t.end)
                {
                    return 5;
                }
                if (t.start >= 1000 || t.end > 1000)
                {
                    return 6;
                }
            }
        }

        for (auto &pts : protocolRxTimimgs)
        {
            // 3) timing span must not be empty except PROTOCOL_NONE
            if (pts.radioConfig.protocol() != GATAS::ConspicuityProtocol::NONE && pts.timeSlots.size() == 0)
            {
                return 13;
            }

            if (pts.zone == CountryRegulations::Zone::ZONE0)
            {
                return 17;
            }

            if (GATAS::radioProtocolIndex(pts.radioConfig.protocol()) >= GATAS::RADIO_PROTOCOLS.size())
            {
                return 14;
            }

            // 4) Validate each ChannelTiming
            for (auto &t : pts.timeSlots)
            {
                if (t.start == t.end)
                {
                    return 15;
                }
                if (t.start >= 1000 || t.end > 1000)
                {
                    return 16;
                }
            }
        }

        // 5) Combined ChannelTimings for any (zone, protocol) group must fit in MAX_COMBINED_TIMINGS
        for (size_t i = 0; i < protocolTxTimimgs.size(); ++i)
        {
            const auto &pts = protocolTxTimimgs[i];
            // Skip entries that are not the start of their group
            if (i > 0 &&
                protocolTxTimimgs[i - 1].radioConfig.protocol() == pts.radioConfig.protocol() &&
                protocolTxTimimgs[i - 1].zone == pts.zone)
            {
                continue;
            }
            size_t combinedCount = 0;
            for (size_t j = i; j < protocolTxTimimgs.size(); ++j)
            {
                const auto &entry = protocolTxTimimgs[j];
                if (entry.radioConfig.protocol() == pts.radioConfig.protocol() && entry.zone == pts.zone)
                {
                    combinedCount += entry.timeSlots.size();
                }
                else
                {
                    break;
                }
            }
            if (combinedCount > MAX_COMBINED_TIMINGS)
            {
                return 8;
            }
        }

        return 0;
    }

private:
    CountryRegulations() = delete;

public:
    /**
     * Based on current lat/long get the current regulation zone
     */
    static Zone zone(float lat, float lon);

    /**
     * @brief get the correct slot for the protocol and Zone. This decides everything on what frequency to use, timings etc..
     *
     * @param zone
     * @param protocol
     * @return const CountryRegulations::ProtocolTimeSlot& Reference to the slot configuration
     */
    static etl::span<const ProtocolTxTimeSlot> getProtocolTxTimings(CountryRegulations::Zone zone, GATAS::ConspicuityProtocol protocol);
    static const CountryRegulations::ProtocolRxTimeSlot &getProtocolRxTimings(CountryRegulations::Zone zone, GATAS::ConspicuityProtocol protocol);

    /**
     * @brief Decide on the frequency from the configuration and the channel
     * Note: Only CH00 and CH01 is supported, NOOP is not a valid channel
     *
     * @param frequency
     * @param channel
     * @return uint32_t frequency in Hz
     */
    static uint32_t getFrequency(const GATAS::RfConfig &frequency, CountryRegulations::Channel channel);

    /**
     * @brief Calculate the next random TX delay that lands within any timeslot across
     * all entries in the span. When a protocol has multiple ProtocolTxTimeSlot entries
     * (e.g. TRAFFIC on 500–900 ms and UPLINK on 200–450 ms), passing the full span ensures
     * that both windows are considered so neither is permanently starved.
     *
     * txMinTime and txMaxTime are taken from the first entry; all entries in a group
     * must share the same values (enforced by validateProtocolTxTimings).
     *
     * @param reducedTxRate  when true, slows down TX rate. Used for on ground operations
     * @param timing  Span of one or more ProtocolTxTimeSlot entries for the same protocol.
     * @return Delay in ms, or UINT32_MAX if no suitable slot was found after MAX_TRIES.
     */
    static uint32_t nextRandomTxTime(bool reducedTxRate, const CountryRegulations::ProtocolTxTimeSlot &timing);

public:
    /**
     * @brief Calculate the frequency channel based on a timestamp.
     *
     * @param timestamp
     * @param nch
     * @return uint32_t
     */
    static uint32_t frequencyByTimestamp(uint32_t timestamp, uint32_t nch);

    /**
     * Tests if a given timing (in ms) falls within the active window defined by a ChannelTiming.
     * The timing is normalized to a 0–999 ms range, and the function correctly handles windows that wrap around the end of the second (e.g. 800–200 ms).
     */
    static bool isInTiming(uint32_t ms, const ChannelTiming &t)
    {
        // ms is in range 0..999
        ms = ms % 1000;
        uint32_t start = t.start % 1000;
        uint32_t end = (t.end - ChannelTiming::REDUCE_ENDTIME_MS) % 1000;

        if (start < end)
        {
            // Normal case: e.g. 400..800
            return (ms >= start) && (ms < end);
        }
        else
        {
            // Wrapped case: e.g. 800..1200 == 800..999 and 0..199
            return (ms >= start) || (ms < end);
        }
    }

    static bool fitsAnyTiming(uint32_t ms, etl::span<const ChannelTiming> timings)
    {
        for (const auto &t : timings)
        {
            if (isInTiming(ms, t))
            {
                return true;
            }
        }
        return false;
    }

    static const ChannelTiming *findFittingTiming(uint32_t ms, etl::span<const ChannelTiming> timings)
    {
        for (const auto &t : timings)
        {
            if (isInTiming(ms, t))
            {
                return &t;
            }
        }
        return nullptr;
    }

    static etl::vector<const ProtocolRxTimeSlot *, GATAS_MAX_SOURCE_PER_RADIO * GATAS_MAX_RADIOS> getProtocolRxTimingsForZone(Zone zone, const etl::span<const GATAS::ProtocolConfig> protocols)
    {
        etl::vector<const ProtocolRxTimeSlot *, GATAS_MAX_SOURCE_PER_RADIO * GATAS_MAX_RADIOS> result;
        if (etl::any_of(protocols.begin(), protocols.end(), [](const auto &config)
                        { return config.protocol == GATAS::ConspicuityProtocol::ADSB && config.isRx(); }))
        {
            result.push_back(&ADSB_RX_TIMESLOT);
        }
        for (const auto &slot : protocolRxTimimgs)
        {
            auto isDs = etl::find_if(protocols.cbegin(), protocols.cend(), [slot](GATAS::ProtocolConfig ds)
                                     { return slot.radioConfig.isRxProtocol(ds.protocol) && ds.isRx(); });

            if (slot.zone == zone && !result.full() && (isDs != protocols.cend()))
            {
                result.push_back(&slot);
            }
        }

        return result;
    }
};

static_assert(CountryRegulations::validateProtocolTxTimings() == 0, "ProtocolTxTimings validation failed");
