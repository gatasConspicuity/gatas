#pragma once

#include "etl/bitset.h"
#include "etl/span.h"
#include "etl/array.h"

namespace GATAS
{

    /**
     * @brief Helper class to store reception times of messages from different data sources.
     *        This can be used to understand if timings are correct.
     */
    class ConspicuityProtocolTimeStatsTableBase
    {
    public:
        struct ConspicuityProtocolTimeStats
        {
            etl::bitset<100> timeTenthMs{};
            uint32_t frequency = 0;
        };

        using const_span_type = etl::span<const ConspicuityProtocolTimeStats>;
        using mutable_span_type = etl::span<ConspicuityProtocolTimeStats>;

        void addReceiveStat(uint32_t frequency, uint16_t msInSecond)
        {
            GATAS_ASSERT(msInSecond < 1000, "Must be smaller than 1000");

            auto tenthMsIndex = 99 - msInSecond / 10;

            for (auto &&stat : used_span())
            {
                if (stat.frequency == frequency)
                {
                    stat.timeTenthMs.set(tenthMsIndex);
                    return;
                }
            }

            if (size_ < storage_.size())
            {
                auto &stat = storage_[size_++];
                stat = ConspicuityProtocolTimeStats{};
                stat.frequency = frequency;
                stat.timeTenthMs.set(tenthMsIndex);
            }
        }

        const_span_type span() const { return const_span_type(storage_.data(), size_); }
        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }
        void clear() { size_ = 0; }

    protected:
        ConspicuityProtocolTimeStatsTableBase() = default;

        void init(mutable_span_type storage)
        {
            GATAS_ASSERT(storage.data() != nullptr, "Storage must be valid");
            storage_ = storage;
            size_ = 0;
        }

        mutable_span_type used_span()
        {
            return mutable_span_type(storage_.data(), size_);
        }

    private:
        mutable_span_type storage_{};
        size_t size_ = 0;
    };

    // Templated wrapper — only owns storage, zero logic here
    template <size_t MaxSources>
    class ConspicuityProtocolTimeStatsTable : public ConspicuityProtocolTimeStatsTableBase
    {
    public:
        ConspicuityProtocolTimeStatsTable()
        {
            init(mutable_span_type(buffer_.data(), MaxSources));
        }

        void toStream(etl::string_stream &stream) const
        {
            for (const auto &stat : span())
            {
                stream << "\"" << stat.frequency << ":dts\":\"" << stat.timeTenthMs.to_string() << "\",";
            }
        }

    private:
        etl::array<ConspicuityProtocolTimeStats, MaxSources> buffer_{};
    };

} // namespace GATAS
