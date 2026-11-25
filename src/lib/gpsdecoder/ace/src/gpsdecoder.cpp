#include <stdio.h>

#include "../gpsdecoder.hpp"
#include "ace/ownshipstate.hpp"
#include "ace/moreutils.hpp"
#include "etl/algorithm.h"

extern const char *GATAS_BUILD_TIMESTAMP;
extern const char *GATAS_BUILD_GIT_TAG;

// Address field
// The address field starts with “$” followed by the talker ID and a sentence identifier. The used talker IDs are:
// * $P for GPS only solutions
// * $GL for GLONASS only solutions
// * $GA for GALILEO only solutions
// * $GN for multi GNSS solutions
// The used sentence identifiers are:
// * GGA – Global Positioning System Fix Data
// * VTG – Course over Ground and Ground Speed
// * GSA – GNSS DOP and Active Satellites
// * GSV – GNSS Satellites in View
// * RMC – Recommended Minimum Specific GNSS Data
// * ZDA – Time and Date

GATAS::PostConstruct GpsDecoder::postConstruct()
{
    return GATAS::PostConstruct::OK;
}

void GpsDecoder::start()
{
    getBus().subscribe(*this);
}

void GpsDecoder::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    const auto pressureAltitude = ownshipState.calculatePressureAltitude();
    constexpr etl::format_spec width2fill0 = etl::format_spec().width(2).fill('0');
    const char *dopValue = GATAS::DOPInterpretationToString(GATAS::floatToDOPInterpretation(pDop));
    stream << "{";
    stream << "\"GATAS_BUILD_TIMESTAMP\": \"" << GATAS_BUILD_TIMESTAMP << "\"";
    stream << ",\"GATAS_BUILD_GIT_TAG\": \"" << GATAS_BUILD_GIT_TAG << "\"";
    stream << ",\"receivedGGA:k\":" << statistics.receivedGGA;
    stream << ",\"receivedRMC:k\":" << statistics.receivedRMC;
    stream << ",\"receivedGSA:k\":" << statistics.receivedGSA;
    stream << ",\"receivedGSV:k\":" << statistics.receivedGSV;
    stream << ",\"receivedOther:k\":" << statistics.receivedOther;
    stream << ",\"latitude\":" << etl::format_spec{}.precision(5) << latitude;
    stream << ",\"longitude\":" << longitude << etl::format_spec{}.precision(1);
    stream << ",\"altitudeGeoid:ft\":" << altitudeGeoid();
    stream << ",\"pressureAltitude:ft\":";
    if (!pressureAltitude)
    {
        stream << "null";
    }
    else
    {
        stream << (pressureAltitude.value());
    }
    stream << ",\"geoidSeparation:ft\":" << geoidSeparation;
    stream << ",\"groundspeed:kt\":" << groundSpeed;
    stream << ",\"track:deg\":" << course();
    stream << ",\"pDop\":" << pDop;
    stream << ",\"heightAboveGps:m\":" << heightAboveGps;
    stream << ",\"groundStation:b\":" << groundStation;
    stream << ",\"hDop\":" << hDop << GATAS::RESET_FORMAT;
    stream << ",\"dopValue\":\"" << dopValue << "\"";
    stream << ",\"gpsRate:psec\":" << getGpsRate();
    stream << ",\"fixQuality\":" << fixQuality;
    stream << ",\"gpsFixType\":\"" << fixType.c_str() << "\"";
    stream << ",\"satsUsedForFix\":" << satsUsedForFix;
    stream << ",\"epoch\":" << CoreUtils::secondsSinceEpoch();
    stream << ",\"satsInView\":" << (satViewStats.bds + satViewStats.gal + satViewStats.glo + satViewStats.gps);
    stream << ",\"upTime:el\":" << (CoreUtils::timeS32() - statistics.startTime),
        stream << ",\"UtcTimeMsg\":" << "\""
               << width2fill0 << lastGGATimestamp.hours << GATAS::RESET_FORMAT << ":"
               << width2fill0 << lastGGATimestamp.minutes << GATAS::RESET_FORMAT << ":"
               << width2fill0 << lastGGATimestamp.seconds << GATAS::RESET_FORMAT << "\"";
    stream << "}";
}

void GpsDecoder::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
      if (msg.moduleName == Configuration::NAME)
      {
          conspicuity = msg.config.gaTasConfig().conspicuity;
          heightAboveGps = etl::clamp(conspicuity.heightAboveGps, static_cast<int16_t>(0), static_cast<int16_t>(1500));
          groundStation = conspicuity.groundStation;
      }
}

void GpsDecoder::on_receive(const GATAS::GPSSentenceMsg &msg)
{
    // printf("GpsDecoder: %s\n", msg.sentence.c_str());
    static Every<uint32_t, 10'000'000, 15'000'000> gpsStatsMsg{0}; // Every 30 seconds 10 seconds in

    switch (minmea_sentence_id(msg.sentence.c_str(), false))
    {
    case MINMEA_SENTENCE_GSV:
    {
        struct minmea_sentence_gsv frame;
        statistics.receivedGSV += 1;
        if (minmea_parse_gsv(&frame, msg.sentence.c_str()))
        {
            char type = msg.sentence[2];
            switch (type)
            {
            case 'P':
                satViewStats.gps = frame.total_sats;
                break;
            case 'L':
                satViewStats.glo = frame.total_sats;
                break;
            case 'A':
                satViewStats.gal = frame.total_sats;
                break;
            case 'B':
                satViewStats.bds = frame.total_sats;
                break;
            }
        }
    }
    break;
    case MINMEA_SENTENCE_RMC:
    {
        statistics.receivedRMC += 1;
        struct minmea_sentence_rmc frame;
        if (minmea_parse_rmc(&frame, msg.sentence.c_str()))
        {
            uint16_t millis = frame.time.microseconds / 1000;

#if GATAS_DEBUG == 1
            // Print the time difference between the RMC and the local time
            // It's required to have this within a second accurate since some protocols like FLARM
            // depends on high accurate seconds since EPOCH
            auto msSinceEpoch = static_cast<uint32_t>(CoreUtils::msSinceEpoch() % 1000);
            auto secondsSinceEpoch = CoreUtils::secondsSinceEpoch();
            auto msInSecond = CoreUtils::msInSecond();

            time_t t = (time_t)secondsSinceEpoch;
            struct tm *timeinfo = localtime(&t); // or gmtime() for UTC

            const int32_t localMillisecondsOfDay =
                ((timeinfo->tm_hour * 60 + timeinfo->tm_min) * 60 + timeinfo->tm_sec) * 1'000 + msSinceEpoch;
            const int32_t rmcMillisecondsOfDay =
                ((frame.time.hours * 60 + frame.time.minutes) * 60 + frame.time.seconds) * 1'000 + millis;
            int32_t timeDifferenceMs = localMillisecondsOfDay - rmcMillisecondsOfDay;
            constexpr int32_t millisecondsPerDay = 24 * 60 * 60 * 1'000;
            if (timeDifferenceMs > millisecondsPerDay / 2)
            {
                timeDifferenceMs -= millisecondsPerDay;
            }
            else if (timeDifferenceMs < -millisecondsPerDay / 2)
            {
                timeDifferenceMs += millisecondsPerDay;
            }

            // RMC can arrive just after the local clock crosses a second boundary.
            // Allow the expected sentence/transport delay before reporting drift.
            constexpr int32_t maximumExpectedDifferenceMs = 1'200;
            const int32_t absoluteTimeDifferenceMs = timeDifferenceMs < 0 ? -timeDifferenceMs : timeDifferenceMs;
            if (absoluteTimeDifferenceMs > maximumExpectedDifferenceMs &&
                secondsSinceEpoch > 1000'000'000)
            {

                GATAS_WARN("RMC and Local time don't match %02d:%02d:%02d.%03ld RMC:%02d:%02d:%02d.%03d",
                           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, msSinceEpoch,
                           frame.time.hours, frame.time.minutes, frame.time.seconds, msInSecond);
            }
#endif

            // Send only 2 times per minute
            if ((frame.time.seconds == 0 || frame.time.seconds == 30 || fixType == GATAS::GpsFixType::NO_FIX) && millis == 0)
            {
                // DO we send here the PPS event for software?

                // The time in a RMC sentence is the UTC time, not GPS time
                getBus().receive(
                    GATAS::UtcTimeMsg(
                        static_cast<int16_t>(frame.date.year + 2000),
                        static_cast<int8_t>(frame.date.month),
                        static_cast<int8_t>(frame.date.day),
                        static_cast<int8_t>(frame.time.hours),
                        static_cast<int8_t>(frame.time.minutes),
                        static_cast<int8_t>(frame.time.seconds),
                        static_cast<int16_t>(millis)));
            }

            // Update planes position when fix is valid
            if (frame.valid)
            {
                if (frame.latitude.scale == 0 || frame.longitude.scale == 0)
                {
                    return;
                }

                // float prevLatitude = latitude;
                // float prevLongitude = longitude;

                latitude = (minmea_tocoord(&frame.latitude));
                longitude = (minmea_tocoord(&frame.longitude));

                // TODO: Perhaps use Speed over ground/course over ground?
//                auto const relNorthrelEast = CoreUtils::northEastDistance(prevLatitude, prevLongitude, latitude, longitude);
//                velocityNorth = relNorthrelEast.north;
//                velocityEast = relNorthrelEast.east;

                groundSpeed = getFloat(frame.speed, groundSpeed) * KN_TO_MS;

                // Course might not always be available and will result in a inf values in the filter
                if (frame.course.scale != 0)
                {
                    course(getFloat(frame.course, course()));
                }
                lastRMCTimestamp = frame.time;
                sendMessageWhenGGAisRMC();
            }
        }
    }
    break;

    case MINMEA_SENTENCE_GGA:
    {
        statistics.receivedGGA += 1;

        struct minmea_sentence_gga frame;
        if (minmea_parse_gga(&frame, msg.sentence.c_str()))
        {

            // https://www.unavco.org/software/geodetic-utilities/geoid-height-calculator/geoid-height-calculator.html
            auto geoidAltitude = convertToMeters(frame.altitude, frame.altitude_units, altitudeGeoid()); // Field 9 (MSL)
            geoidSeparation = convertToMeters(frame.height, frame.height_units, geoidSeparation);        // Field 11 (Undulation)
            altitudeGeoid(geoidAltitude);

            satsUsedForFix = frame.satellites_tracked;
            fixQuality = frame.fix_quality;
            lastGGATimestamp = frame.time;

            sendMessageWhenGGAisRMC();
        }
    }
    break;

    case MINMEA_SENTENCE_GSA:
    {
        statistics.receivedGSA += 1;

        if (gpsStatsMsg.isItTime(CoreUtils::timeUs32Raw()))
        {
            struct minmea_sentence_gsa frame;
            if (minmea_parse_gsa(&frame, msg.sentence.c_str()))
            {
                pDop = getFloat(frame.pdop, 100);
                hDop = getFloat(frame.hdop, 100);
                vDop = getFloat(frame.vdop, 100);

                // 2=2D 3=3D
                switch (frame.fix_type)
                {
                case 2:
                    fixType = GATAS::GpsFixType::D2;
                    break;
                case 3:
                    // 0=Fix Not Valid / 1=GPS Fix / 2=DGPS
                    if (fixQuality == 2)
                    {
                        fixType = GATAS::GpsFixType::DGPS;
                    }
                    else if (fixQuality == 1)
                    {
                        fixType = GATAS::GpsFixType::D3;
                    }
                    else
                    {
                        fixType = GATAS::GpsFixType::NO_FIX;
                    }
                    break;
                default:
                    fixType = GATAS::GpsFixType::NO_FIX;
                }

                getBus().receive(
                    GATAS::GpsStatsMsg{GATAS::GpsStats{
                        GATAS::GpsFix{fixType},
                        satsUsedForFix,
                        pDop,
                        hDop,
                        vDop}});
            }
        }
    }
    break;

    default:
    {
        statistics.receivedOther += 1;
    }
    }
}

/**
 * Convert an minmea_float with altitude/height information in meters
 */
float GpsDecoder::convertToMeters(const minmea_float &f, char unit, float defaultValue) const
{
    if (f.scale == 0)
    {
        return defaultValue;
    }
    float meters = minmea_tofloat(&f);
    switch (unit)
    {
    case 'M':
    case 'm':
        return meters;
    case 'F':
    case 'f':
        return meters * FT_TO_M;
    default:
        return defaultValue;
    }
}

/**
 * Send message when both GGA and RMC sentences are received
 */
void GpsDecoder::sendMessageWhenGGAisRMC()
{
    //    GATAS_MEASURE("");
    // Send message over bus when both GGA and GMC sentences are received at the same time
    // It's required that both GGA and GMC sentences have the same timestamp in these cases
    // If this in practise is not happening, due to newer GPS systems position should be taken from latest RMC
    // so we take position acuracy over altitude/course
    // TODO: Reconsider
    if (lastGGATimestamp.microseconds == lastRMCTimestamp.microseconds && lastGGATimestamp.seconds == lastRMCTimestamp.seconds)
    {
        auto altGeoid = altitudeGeoid();

        // TODO: Can we get bank angle from turnrate?? https://aviation.stackexchange.com/questions/65628/what-is-the-formula-for-the-bank-angle-required-for-a-turn-in-line-abreast-forma
        GATAS::OwnshipPositionInfo position{};
        position.timestamp = CoreUtils::timeUs32();
        position.lat = latitude;
        position.lon = longitude;
        position.ellipseHeight = static_cast<int16_t>(altGeoid + geoidSeparation + heightAboveGps);
        position.verticalSpeed = altitudeGeoid.perSecond(); // vertical speed
        position.groundSpeed = groundSpeed; // Ground Speed
        position.track = course();
        position.hTurnRate = course.perSecond(); // degrees per second
//                    .velocityNorth = velocityNorth,
//                    .velocityEast = velocityEast,
        position.geoidSeparation = static_cast<int16_t>(geoidSeparation);
        position.airborne = CoreUtils::isAirborn(conspicuity.category, groundSpeed);
        position.conspicuity = conspicuity;
        ownshipState.location.store(position);
        getBus().receive(GATAS::OwnshipPositionMsg{});
    }
}
