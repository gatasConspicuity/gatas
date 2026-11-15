#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ace/constants.hpp"
#include "ace/coreutils.hpp"
#include "ace/messagerouter.hpp"
#include "mockconfig.h"
#include "pico/time.h"
#include "semphr.h"

#define private public
#include "StaticGPS.hpp"
#undef private

#include "etl/array.h"
#include "etl/vector.h"

#include "minmea.h"

namespace
{
    etl::vector<GATAS::NMEAString, 6> publishedSentences;

    class StaticGpsConfig : public MockConfig
    {
    public:
        explicit StaticGpsConfig(etl::imessage_bus &bus) : MockConfig(bus)
        {
        }

        float floatValueByPath(float defaultValue,
                               const etl::string_view pathToValue,
                               const etl::string_view key) const override
        {
            if (pathToValue != StaticGPS::NAME)
            {
                return defaultValue;
            }
            if (key == "latitude")
            {
                return 52.8925725F;
            }
            if (key == "longitude")
            {
                return 4.7362312F;
            }
            if (key == "altitude")
            {
                return 12.3F;
            }
            return defaultValue;
        }

        const GATAS::ConfigString strValueByPath(const etl::string_view defaultValue,
                                                 const etl::string_view pathToValue,
                                                 const etl::string_view key) const override
        {
            (void)pathToValue;
            (void)key;
            return GATAS::ConfigString(defaultValue);
        }
    };

    void addChecksum(GATAS::NMEAString &sentence)
    {
        CoreUtils::addChecksumToNMEA(sentence, false);
    }

    bool validSentence(const GATAS::NMEAString &sentence)
    {
        return !sentence.ends_with("\r\n") && CoreUtils::validateNMEAChecksum(sentence);
    }
}

BaseModule::BaseModule(etl::imessage_bus &bus_, const etl::string_view name_)
    : bus(bus_), moduleName(name_)
{
}

BaseModule *BaseModule::moduleByName(const BaseModule &that, const etl::string_view requesting)
{
    (void)that;
    (void)requesting;
    return nullptr;
}

GATAS::PostConstruct AbstractGnss::postConstruct()
{
    return GATAS::PostConstruct::OK;
}

void AbstractGnss::start()
{
}

void AbstractGnss::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)stream;
    (void)path;
}

bool AbstractGnss::isPpsDetected()
{
    return false;
}

void AbstractGnss::processNewSentenceFromISR(const etl::array_view<char> &sentence)
{
    processNewSentenceFromTask(sentence);
}

void AbstractGnss::processNewSentenceFromTask(const etl::array_view<char> &sentence)
{
    GATAS::NMEAString copy;
    copy.assign(sentence.begin(), sentence.end());
    publishedSentences.push_back(copy);
}

NtpClient::NtpClient(TimeCallback timeCallback_, PpsCallback ppsCallback_, FailureCallback failureCallback_)
    : timeCallback(timeCallback_),
      ppsCallback(ppsCallback_),
      failureCallback(failureCallback_)
{
}

NtpClient::~NtpClient() = default;

void NtpClient::setServerName(etl::string_view serverName_)
{
    server.assign(serverName_.begin(), serverName_.end());
}

NtpClient::ServerName NtpClient::serverName() const
{
    return server;
}

bool NtpClient::requestTime()
{
    return false;
}

TEST_CASE("StaticGPS NMEA sentences are accepted by the GPS decoder parser")
{
    GATAS::ThreadSafeBus<4> bus;
    StaticGpsConfig config(bus);
    xSemaphoreTakeValue = pdTRUE;
    time_us_64_SET(0);
    CoreUtils::setOffsetMsSinceEpoch(1'787'769'125'500ULL);

    StaticGPS staticGps(bus, config);
    publishedSentences.clear();
    staticGps.publishSentences();

    REQUIRE(publishedSentences.size() == 6);
    for (const auto &sentence : publishedSentences)
    {
        REQUIRE(validSentence(sentence));
    }

    minmea_sentence_gll gll = {};
    minmea_sentence_rmc rmc = {};
    minmea_sentence_gga gga = {};
    minmea_sentence_gsa gsa = {};
    minmea_sentence_gsv gsvPage1 = {};
    minmea_sentence_gsv gsvPage2 = {};

    REQUIRE(minmea_parse_gll(&gll, publishedSentences[0].c_str()));
    REQUIRE(minmea_parse_rmc(&rmc, publishedSentences[1].c_str()));
    REQUIRE(minmea_parse_gga(&gga, publishedSentences[2].c_str()));
    REQUIRE(minmea_parse_gsa(&gsa, publishedSentences[3].c_str()));
    REQUIRE(minmea_parse_gsv(&gsvPage1, publishedSentences[4].c_str()));
    REQUIRE(minmea_parse_gsv(&gsvPage2, publishedSentences[5].c_str()));

    INFO("GLL: " << publishedSentences[0].c_str());
    REQUIRE(gll.status == 'A');
    REQUIRE(minmea_tocoord(&gll.latitude) == Catch::Approx(52.8925725F));
    REQUIRE(minmea_tocoord(&gll.longitude) == Catch::Approx(4.7362312F));
    REQUIRE(gll.time.hours == 18);
    REQUIRE(gll.time.minutes == 32);
    REQUIRE(gll.time.seconds == 5);
    REQUIRE(gll.time.microseconds == 500'000);
    REQUIRE(rmc.valid);
    REQUIRE(rmc.date.day == 26);
    REQUIRE(rmc.date.month == 8);
    REQUIRE(rmc.date.year == 26);
    REQUIRE(rmc.speed.value == 0);
    REQUIRE(rmc.course.value == 0);
    REQUIRE(gga.fix_quality == 1);
    REQUIRE(gga.satellites_tracked == 8);
    REQUIRE(minmea_tofloat(&gga.altitude) == Catch::Approx(12.3F));
    REQUIRE(gsa.mode == 'M');
    REQUIRE(gsa.fix_type == 3);
    REQUIRE(gsvPage1.total_sats == 8);
    REQUIRE(gsvPage2.total_sats == 8);
}

TEST_CASE("StaticGPS NMEA sentences report no fix before time synchronization")
{
    GATAS::NMEAString gll = "$GPGLL,5253.55446,N,00444.17388,E,000012.50,V";
    GATAS::NMEAString rmc = "$GPRMC,000012.50,V,5253.55446,N,00444.17388,E,0.000,0.00,010170,,";
    GATAS::NMEAString gga = "$GPGGA,000012.50,5253.55446,N,00444.17388,E,0,00,1.0,12.3,M,42.0,M,,";
    GATAS::NMEAString gsa = "$GPGSA,M,1,,,,,,,,,,,,,1.0,1.0,1.0";

    addChecksum(gll);
    addChecksum(rmc);
    addChecksum(gga);
    addChecksum(gsa);

    minmea_sentence_gll gllFrame = {};
    minmea_sentence_rmc rmcFrame = {};
    minmea_sentence_gga ggaFrame = {};
    minmea_sentence_gsa gsaFrame = {};

    REQUIRE(minmea_parse_gll(&gllFrame, gll.c_str()));
    REQUIRE(minmea_parse_rmc(&rmcFrame, rmc.c_str()));
    REQUIRE(minmea_parse_gga(&ggaFrame, gga.c_str()));
    REQUIRE(minmea_parse_gsa(&gsaFrame, gsa.c_str()));
    REQUIRE(gllFrame.status == 'V');
    REQUIRE_FALSE(rmcFrame.valid);
    REQUIRE(ggaFrame.fix_quality == 0);
    REQUIRE(ggaFrame.satellites_tracked == 0);
    REQUIRE(gsaFrame.mode == 'M');
    REQUIRE(gsaFrame.fix_type == 1);
}

TEST_CASE("StaticGPS NMEA contract supports southern and western coordinates")
{
    GATAS::NMEAString sentence = "$GPGLL,3330.00000,S,07045.00000,W,183205.50,A";
    addChecksum(sentence);

    minmea_sentence_gll gll = {};
    REQUIRE(validSentence(sentence));
    REQUIRE(minmea_parse_gll(&gll, sentence.c_str()));
    REQUIRE(gll.latitude.value < 0);
    REQUIRE(gll.longitude.value < 0);
}

TEST_CASE("StaticGPS confirms large NTP corrections before changing time or PPS")
{
    class TestRtc : public RtcModule
    {
    public:
        explicit TestRtc(etl::imessage_bus &bus) : RtcModule(bus) {}
        GATAS::PostConstruct postConstruct() override { return GATAS::PostConstruct::OK; }
        void start() override {}
        void ppsEvent(int32_t offsetUs) override
        {
            lastOffset = offsetUs;
            calls++;
        }
        int32_t lastOffset = 0;
        unsigned calls = 0;
    };

    GATAS::ThreadSafeBus<4> bus;
    StaticGpsConfig config(bus);
    StaticGPS gps(bus, config);
    TestRtc rtc(bus);
    gps.rtc = &rtc;
    time_us_64_SET(0);
    constexpr uint64_t epoch = 1'787'769'125'000ULL;
    CoreUtils::setOffsetMsSinceEpoch(epoch);

    auto sample = [&](int64_t offsetMs)
    {
        gps.ntpTimeResult.store(NtpTimeResult{static_cast<uint64_t>(static_cast<int64_t>(CoreUtils::msSinceEpoch()) + offsetMs), CoreUtils::monotonic()});
        return gps.applyNtpResult();
    };

    REQUIRE_FALSE(sample(200));
    REQUIRE(CoreUtils::msSinceEpoch() == epoch);
    REQUIRE(rtc.calls == 0);
    REQUIRE_FALSE(sample(-200)); // A disagreement starts a new sequence.
    REQUIRE_FALSE(sample(-220));
    REQUIRE(sample(-210));
    REQUIRE(CoreUtils::msSinceEpoch() == epoch - 210);
    REQUIRE(rtc.calls == 1);
    REQUIRE(rtc.lastOffset == 790'000);

    REQUIRE_FALSE(sample(300));
    REQUIRE(sample(20)); // A normal sample clears the pending jump.
    REQUIRE_FALSE(sample(300));
    REQUIRE(rtc.calls == 2);
}
