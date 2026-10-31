#include "../StaticGPS.hpp"

#include <cmath>
#include <inttypes.h>
#include <ctime>

#include "ace/coreutils.hpp"
#include "ace/semaphoreguard.hpp"

#include "etl/string_stream.h"

StaticGPS::StaticGPS(etl::imessage_bus &bus, const Configuration &config)
    : AbstractGnss(bus, NAME, GATAS::PinTypeMap{}, true, 0, false),
      locationData(CoreUtils::sharedSpinLock()),
      newServerName(CoreUtils::sharedSpinLock()),
      ntpClient(NtpClient::TimeCallback::create<StaticGPS, &StaticGPS::onNtpTime>(*this),
                NtpClient::PpsCallback::create<StaticGPS, &StaticGPS::onNtpPps>(*this),
                NtpClient::FailureCallback::create<StaticGPS, &StaticGPS::onNtpFailure>(*this)),
      ntpTimeResult(CoreUtils::sharedSpinLock()), ntpSlewUs(CoreUtils::sharedSpinLock())
{
    readConfiguration(config);
}

void StaticGPS::readConfiguration(const Configuration &config)
{
    float latitude = config.floatValueByPath(0.0F, NAME, "latitude");
    float longitude = config.floatValueByPath(0.0F, NAME, "longitude");
    float altitudeMeters = config.floatValueByPath(0.0F, NAME, "altitude");
    ServerName serverName = config.strValueByPath("time.cloudflare.com", NAME, "ntpServer");

    if (!std::isfinite(latitude) || latitude < -90.0F || latitude > 90.0F)
    {
        latitude = 0.0F;
    }
    if (!std::isfinite(longitude) || longitude < -180.0F || longitude > 180.0F)
    {
        longitude = 0.0F;
    }
    if (!std::isfinite(altitudeMeters) || altitudeMeters < -1000.0F || altitudeMeters > 20000.0F)
    {
        altitudeMeters = 0.0F;
    }
    if (serverName.empty())
    {
        serverName = "time.cloudflare.com";
    }
    newServerName.store(serverName);

    const float geoidSeparationMeters = static_cast<float>(CoreUtils::egmGeoidOffset(latitude, longitude));
    const LocationData newLocation{latitude, longitude, altitudeMeters, geoidSeparationMeters};
    locationData.store(newLocation);
}

GATAS::PostConstruct StaticGPS::postConstruct()
{
#if !MEASURE_NTP_OFFSET
    if (BaseModule::moduleByName(*this, "L76B") != nullptr ||
        BaseModule::moduleByName(*this, "UbloxM8N") != nullptr)
    {
        setStatus("GNSS conflict");
        return GATAS::PostConstruct::CONFIG_ERROR;
    }
#endif

    rtc = static_cast<RtcModule *>(moduleByName(*this, RtcModule::NAME));
    if (rtc == nullptr)
    {
        setStatus("No RTC");
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }

    const GATAS::PostConstruct result = AbstractGnss::postConstruct();
    if (result != GATAS::PostConstruct::OK)
    {
        return result;
    }

    if (xTaskCreate(taskTrampoline, NAME.cbegin(), configMINIMAL_STACK_SIZE + 256, this, tskIDLE_PRIORITY, &staticTaskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }

    setStatus("Waiting WiFi");
    return GATAS::PostConstruct::OK;
}

void StaticGPS::start()
{
    AbstractGnss::start();
    if (!receiveTaskReady())
    {
        vTaskDelete(staticTaskHandle);
        staticTaskHandle = nullptr;
        return;
    }

    sendTimerHandle = xTimerCreate(NAME.cbegin(), TASK_DELAY_MS(SEND_INTERVAL_MS), pdTRUE, this, timerCallback);
    if (sendTimerHandle == nullptr || xTimerStart(sendTimerHandle, 0) != pdPASS)
    {
        setStatus("Timer error");
        return;
    }

    getBus().subscribe(*this);
}

void StaticGPS::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName != StaticGPS::NAME && msg.moduleName != Configuration::NAME)
    {
        return;
    }

    readConfiguration(msg.config);
    xTaskNotify(staticTaskHandle, NTP_SERVER_UPDATED, eSetBits);
}

void StaticGPS::taskTrampoline(void *arg)
{
    static_cast<StaticGPS *>(arg)->task();
}

void StaticGPS::timerCallback(TimerHandle_t timer)
{
    auto *staticGps = static_cast<StaticGPS *>(pvTimerGetTimerID(timer));
    xTaskNotify(staticGps->staticTaskHandle, SEND_SENTENCES, eSetBits);
}

void StaticGPS::task()
{
    bool hasNewServerData = false;
    uint64_t nextNtpAttemptUs = 0;

    ntpClient.setServerName(newServerName.load());
    while (true)
    {
        uint32_t notification = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notification, portMAX_DELAY);
        const uint64_t nowUs = CoreUtils::monotonic();

        if ((notification & NTP_RESULT) != 0)
        {
            applyNtpResult();
            nextNtpAttemptUs = nowUs + NTP_REFRESH_INTERVAL_US;
        }

        if ((notification & (NTP_FAILED | NTP_ROUND_TRIP_TOO_LONG)) != 0)
        {
            staticStatistics.ntpErrors += 1;
            nextNtpAttemptUs = nowUs + NTP_RETRY_INTERVAL_US;
        }
        if ((notification & NTP_ROUND_TRIP_TOO_LONG) != 0)
        {
            staticStatistics.ntpRoundTripRejected += 1;
        }

        if ((notification & NTP_SERVER_UPDATED) != 0)
        {
            hasNewServerData = true;
            nextNtpAttemptUs = nowUs;
            // Handle the set of the ntp server right before we do the request for reliability
        }

        if ((notification & NETWORK_CHANGED) != 0)
        {
            if (wifiConnected)
            {
                nextNtpAttemptUs = nowUs;
                setStatus("Waiting NTP");
            }
            else
            {
                setStatus("Waiting WiFi");
            }
        }

        if (wifiConnected && nowUs >= nextNtpAttemptUs)
        {
            staticStatistics.ntpRequests += 1;

            // First copy the data, then apply the NTP result
            if (hasNewServerData && !ntpClient.isPending())
            {
                auto const localNewServerName = newServerName.load();
                ntpClient.setServerName(localNewServerName);
                hasNewServerData = false;
            }
            if (ntpClient.requestTime())
            {
                nextNtpAttemptUs = nowUs + NTP_REFRESH_INTERVAL_US;
            }
            else
            {
                nextNtpAttemptUs = nowUs + NTP_RETRY_INTERVAL_US;
            }
        }

        if ((notification & SEND_SENTENCES) != 0)
        {
            publishSentences();
        }
    }
}

StaticGPS::Coordinate StaticGPS::coordinate(float value, bool latitude)
{
    Coordinate result;
    result.hemisphere = latitude ? (value < 0.0F ? "S" : "N") : (value < 0.0F ? "W" : "E");
    const float absolute = std::fabs(value);
    int degrees = static_cast<int>(absolute);
    float minutes = std::round((absolute - static_cast<float>(degrees)) * 60.0F * 100000.0F) / 100000.0F;
    if (minutes >= 60.0F)
    {
        minutes = 0.0F;
        degrees += 1;
    }

    etl::string_stream stream(result.text);
    stream << etl::format_spec{}.width(latitude ? 2 : 3).fill('0') << degrees
           << etl::format_spec{}.precision(5).width(8).fill('0') << minutes;
    return result;
}

void StaticGPS::publishSentences()
{
#if MEASURE_NTP_OFFSET
    return;
#endif

    const auto currentLocation = locationData.load();

    const uint64_t epochMs = CoreUtils::msSinceEpoch();
    const bool valid = epochMs >= 1'000'000'000'000ULL;
    if (!valid && wifiConnected) // Only make a fair count when wifi is connect
    {
        staticStatistics.invalidTime += 1;
    }

    const Coordinate latitudeCoordinate = coordinate(currentLocation.latitude, true);
    const Coordinate longitudeCoordinate = coordinate(currentLocation.longitude, false);

    const time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
    struct tm utc = {};
    if (gmtime_r(&seconds, &utc) == nullptr)
    {
        return;
    }

    etl::string<9> timeText;
    etl::string_stream timeStream(timeText);
    const uint32_t centiseconds = static_cast<uint32_t>((epochMs % 1000ULL) / 10ULL);
    timeStream << etl::format_spec{}.width(2).fill('0') << utc.tm_hour
               << etl::format_spec{}.width(2).fill('0') << utc.tm_min
               << etl::format_spec{}.width(2).fill('0') << utc.tm_sec
               << GATAS::RESET_FORMAT << "."
               << etl::format_spec{}.width(2).fill('0') << centiseconds;

    etl::string<6> dateText;
    etl::string_stream dateStream(dateText);
    dateStream << etl::format_spec{}.width(2).fill('0') << utc.tm_mday
               << utc.tm_mon + 1
               << (utc.tm_year + 1900) % 100;

    const etl::string_view validity = valid ? ",A" : ",V";
    GATAS::NMEAString nmeaString;
    {
        etl::string_stream stream(nmeaString);
        stream << "$GPGLL," << latitudeCoordinate.text << "," << latitudeCoordinate.hemisphere
               << "," << longitudeCoordinate.text << "," << longitudeCoordinate.hemisphere
               << "," << timeText << validity;
    }
    CoreUtils::addChecksumToNMEA(nmeaString, false);
    processNewSentenceFromTask({nmeaString.data(), nmeaString.size()});

    nmeaString.clear();
    {
        etl::string_stream stream(nmeaString);
        stream << "$GPRMC," << timeText << validity
               << "," << latitudeCoordinate.text << "," << latitudeCoordinate.hemisphere
               << "," << longitudeCoordinate.text << "," << longitudeCoordinate.hemisphere
               << ",0.000,0.00," << dateText << ",,";
    }
    CoreUtils::addChecksumToNMEA(nmeaString, false);
    processNewSentenceFromTask({nmeaString.data(), nmeaString.size()});

    nmeaString.clear();
    {
        etl::string_stream stream(nmeaString);
        stream << "$GPGGA," << timeText << "," << latitudeCoordinate.text << "," << latitudeCoordinate.hemisphere
               << "," << longitudeCoordinate.text << "," << longitudeCoordinate.hemisphere
               << "," << (valid ? "1,08" : "0,00") << ",1.0,"
               << etl::format_spec{}.precision(1) << currentLocation.altitudeMeters << GATAS::RESET_FORMAT << ",M," 
               << etl::format_spec{}.precision(1) << currentLocation.geoidSeparationMeters << GATAS::RESET_FORMAT << ",M,,";
    }
    CoreUtils::addChecksumToNMEA(nmeaString, false);
    processNewSentenceFromTask({nmeaString.data(), nmeaString.size()});

    nmeaString = valid
                     ? "$GPGSA,M,3,03,04,08,10,13,16,21,27,,,,,1.0,1.0,1.0"
                     : "$GPGSA,M,1,,,,,,,,,,,,,1.0,1.0,1.0";
    CoreUtils::addChecksumToNMEA(nmeaString, false);
    processNewSentenceFromTask({nmeaString.data(), nmeaString.size()});

    nmeaString = "$GPGSV,2,1,08,03,45,111,42,04,50,272,43,08,35,046,40,10,60,151,45";
    CoreUtils::addChecksumToNMEA(nmeaString, false);
    processNewSentenceFromTask({nmeaString.data(), nmeaString.size()});

    nmeaString = "$GPGSV,2,2,08,13,25,210,38,16,40,315,41,21,55,080,44,27,30,180,39";
    CoreUtils::addChecksumToNMEA(nmeaString, false);
    processNewSentenceFromTask({nmeaString.data(), nmeaString.size()});
}

void StaticGPS::onNtpTime(const NtpTimeResult &result)
{
    ntpTimeResult.store(result);
    xTaskNotify(staticTaskHandle, NTP_RESULT, eSetBits);
}

void StaticGPS::onNtpPps(int32_t offsetUs)
{
#if MEASURE_NTP_OFFSET
    (void)offsetUs;
#else
    rtc->ppsEvent(offsetUs);
#endif
}

void StaticGPS::onNtpFailure(NtpClient::Failure failure)
{
    const uint32_t notification = failure == NtpClient::Failure::ROUND_TRIP_TOO_LONG
                                      ? NTP_ROUND_TRIP_TOO_LONG
                                      : NTP_FAILED;
    xTaskNotify(staticTaskHandle, notification, eSetBits);
}

void StaticGPS::applyNtpResult()
{
    const uint64_t nowUs = CoreUtils::monotonic();

    const auto localNtpTimeResult = ntpTimeResult.load();
    const uint64_t epochNowMs = localNtpTimeResult.epochMs + (nowUs - localNtpTimeResult.receivedAtUs) / 1'000ULL;
    const uint64_t picoEpochMs = CoreUtils::msSinceEpoch();
    const int64_t slewUs = picoEpochMs >= 1'000'000'000'000ULL
                               ? (epochNowMs >= picoEpochMs
                                      ? static_cast<int64_t>((epochNowMs - picoEpochMs) * 1'000ULL)
                                      : -static_cast<int64_t>((picoEpochMs - epochNowMs) * 1'000ULL))
                               : 0;
    ntpSlewUs.store(slewUs);
#if MEASURE_NTP_OFFSET
    GATAS_INFO("NTP offset from GPS: %" PRId64 "us", slewUs);
#else
    CoreUtils::setOffsetMsSinceEpoch(epochNowMs);
#endif
    staticStatistics.ntpSyncs += 1;

    setStatus(MEASURE_NTP_OFFSET ? "Measuring NTP" : "Configured");
}

void StaticGPS::on_receive(const GATAS::WifiConnectionStateMsg &msg)
{
    wifiConnected = msg.wifiMode == GATAS::WifiMode::CLIENT;

    xTaskNotify(staticTaskHandle, NETWORK_CHANGED, eSetBits);
}

void StaticGPS::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

bool StaticGPS::configureGnss()
{
    return true;
}

void StaticGPS::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    auto const servername = newServerName.load();
    const int64_t currentNtpSlewUs = ntpSlewUs.load();
    const auto currentLocation = locationData.load();
    stream << "{";
    stream << "\"latitude\":" << etl::format_spec{}.precision(7) << currentLocation.latitude;
    stream << ",\"longitude\":" << currentLocation.longitude;
    stream << ",\"altitude:m\":" << currentLocation.altitudeMeters;
    stream << ",\"geoidSeparation:m\":" << currentLocation.geoidSeparationMeters;
    stream << ",\"ntpServer\":\"" << servername << "\"";
    stream << ",\"ntpSlew:usec\":" << currentNtpSlewUs;
    stream << ",\"status\":\"" << statistics.status << "\"";
    stream << ",\"totalReceived:k\":" << statistics.totalReceived;
    stream << ",\"queueFullErr:k\":" << statistics.queueFullErr;
    stream << ",\"ntpRequests:k\":" << staticStatistics.ntpRequests;
    stream << ",\"ntpSyncs:k\":" << staticStatistics.ntpSyncs;
    stream << ",\"ntpErrors:err\":" << staticStatistics.ntpErrors;
    stream << ",\"ntpRoundTripsRejected:err\":" << staticStatistics.ntpRoundTripRejected;
    stream << ",\"invalidTimeCycles:err\":" << staticStatistics.invalidTime;
    stream << "}";
}
