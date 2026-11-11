#pragma once

#include <stdint.h>

#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "ace/coreutils.hpp"

#include "pico/time.h"
#include "sys/time.h"

// When set, also set's the PICO's rtc


class PicoRtc : public RtcModule, public etl::message_router<PicoRtc, GATAS::UtcTimeMsg>
{
    friend class message_router;
    // UtcTimeMsg needs to be ignored when time between PPS and the message is more than this value
    static constexpr const uint32_t HIGH_TIMEMSG_DELAY_THRESHOLD = 300'000; 

    struct
    {
        uint32_t epochSet=0;
        uint32_t delayMs=0; // Delay between PPS and when we received a time message from the GPS
        uint32_t highElapseTimeErr=0;
    } statistics;

private:
    void on_receive(const GATAS::UtcTimeMsg& msg);

    void on_receive_unknown(const etl::imessage& msg);

    uint32_t lastPpstime; // uint32_t since we use it for difference calculations
public:
    static constexpr const etl::string_view NAME = "PicoRtc";
    PicoRtc(etl::imessage_bus& bus, const Configuration &config) :  RtcModule(bus), lastPpstime(0)
    {
        (void)config;
    }

    // Safe only after failed postConstruct(), before start().
    // Construction and postConstruct only initialise owned state.
    bool canDelete() const override
    {
        return true;
    }

    virtual ~PicoRtc() = default;

    virtual void ppsEvent(int32_t offsetUs) override;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;
};
