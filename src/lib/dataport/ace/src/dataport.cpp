#include <stdio.h>

#include "../dataport.hpp"
#include "ace/moreutils.hpp"

#include "lwip/tcp.h"

constexpr const bool DEBUG_DATAPORT = false;

GATAS::PostConstruct DataPort::postConstruct()
{
    return GATAS::PostConstruct::OK;
}

// void DataPort::on_receive(const GATAS::OwnshipPositionMsg &)
// {
//     ownshipPosition = ownshipState.location.load().assignTo();

//     // if (sendValidGps.isItTime(CoreUtils::timeUs32Raw()))
//     // {

//         // Initially the idea was to 'emulate' a GNS chip, but later decides
//         // to pass through GPS messages. Keeping this code just in case/
//         // sendGPRMC(message.position);
//         // sendGPGSA(ownshipPosition);
//         // sendGPGGA(ownshipPosition);
//         // sendPGRMZ(msg.position);

//     // }
// }

void DataPort::on_receive(const GATAS::EgressAircraftPositionMsg &msg)
{
    sendPFLAA(msg.position);
}

void DataPort::on_receive(const GATAS::GpsStatsMsg &msg)
{
    hasGpsFix = msg.gpsStats.gpsFix.hasFix;
}

void DataPort::on_receive(const GATAS::TrackerStatsMsg &msg)
{
    noTrackedAircraft = msg.noTrackedAircraft;
}

void DataPort::on_receive(const GATAS::Every1SecMsg &msg)
{
    (void)msg;
    if (pflauEnabled)
    {
        sendPFLAU();
    }
}

void DataPort::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == DataPort::NAME || msg.moduleName == Configuration::NAME)
    {
        pflauEnabled = msg.config.valueByPath(false, DataPort::NAME, "pflauEnabled");
    }
}

void DataPort::on_receive(const GATAS::GPSSentenceMsg &msg)
{
    GATAS::NMEAString sentence = msg.sentence;

    // Check the message type at position 3–5
    // Drop specific GPS sentences
    char type[4] = {sentence[4], sentence[5], '\0'};
    if (etl::string_view(type) == "SV")
    {
        return;
    }

    sentence.append("\r\n");
    if (DEBUG_DATAPORT)
    {
        puts(sentence.c_str());
    }

    getBus().receive(GATAS::DataPortMsg{sentence});
    statistics.messages += 1;
}

/**
 * PFLAA – Data on other proximate aircraft
 * Periodicity:
 * Sent when available. Can be sent several times per second with information on several (but not always all) surrounding targets.
 */
void DataPort::sendPFLAA(const GATAS::AircraftPositionInfo &position)
{
    GATAS::NMEAString pflaa;
    etl::string_stream stream(pflaa);

    etl::string<6> groundSpeed;
    getPFLAAGroundSpeed(position, groundSpeed);

    etl::string<7> climbRate;
    getPFLAAClimbRate(position, climbRate);

    auto CO = etl::make_string(",");
    auto ownshipPosition = ownshipState.location.load();
    
    auto relativeFromOwn = position.relativeFromOwn(ownshipPosition);

    // PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,<RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<AcftType>[,<NoTrack>[,<Source>,<RSSI>]]
    // Example: $PFLAA,0,10,7,21,0,B1B1B1,0,,,-0.1,1,48,0,*23
    stream << etl::make_string("$PFLAA,"
                               "0,")                                         // Alarm Level
           << relativeFromOwn.relNorth << CO                                 // Relative North Meters
           << relativeFromOwn.relEast << CO                                  // Relative East Meters
           << (position.ellipseHeight - ownshipPosition.ellipseHeight) << CO // Relative Vertical Meters
           << getPFLAAAddressType(position.addressType) << CO;               // ID Type
    CoreUtils::streamIcaoAddress(stream, position.address, position.addressType, position.callSign);
    stream << CO                                       // HEXCode example 484FB3!PH-DHA
           << position.track << CO                    // Track
           << CO                                       // TurnRate kept empty
           << groundSpeed << CO                        // Ground Speed
           << climbRate << CO                          // Climb Rate
           << getPFLAAAircraftCategory(position) << CO // Aircraft Type
           << position.noTrack << CO                   // Tracking
           << getPFLAASourceType(position) << CO       // Source Type
        ;                                              // RSSI

    CoreUtils::addChecksumToNMEA(pflaa);
    // Or use netcat 192.168.178.227 2000 > log.txt
    if (DEBUG_DATAPORT)
    {
        puts(pflaa.c_str());
    }
    // printf("t:%08ld %s", CoreUtils::timeMs32(), pflaa.c_str());
    getBus().receive(GATAS::DataPortMsg{pflaa});
    statistics.messages += 1;
}

uint8_t DataPort::getPFLAASourceType(const GATAS::AircraftPositionInfo &position)
{
    switch (position.protocol)
    {
    case GATAS::ConspicuityProtocol::ADSB:
        return 1;
    default:
        // FLARM OGN FANET ADS-L
        return 0;
    }
}

uint8_t DataPort::getPFLAAAddressType(const GATAS::AddressType type)
{
    //        return type==GATAS::AddressType::OGN?1:static_cast<uint8_t>(type);
    uint8_t itype = static_cast<uint8_t>(type);
    return itype > 2 ? 1 : itype;
}

// Groundspeed is in m/s for PFLAA
void DataPort::getPFLAAGroundSpeed(const GATAS::AircraftPositionInfo &position, etl::string<6> &speed)
{
    // if (position.stealth /* || ownship.notrack */)
    // {
    //     return;
    // }
    // if (!position.airborne)
    // {
    //     return;
    // }

    etl::to_string(static_cast<uint16_t>(position.groundSpeed + 0.5f), speed);
}

void DataPort::getPFLAAClimbRate(const GATAS::AircraftPositionInfo &position, etl::string<7> &verticalSpeed)
{
    etl::format_spec clibRateFormat = etl::format_spec().precision(1);

    // if (position.stealth /* || ownship.privacy */)
    // {
    //     return;
    // }

    etl::to_string(position.verticalSpeed, verticalSpeed, clibRateFormat);
}

etl::string_view DataPort::getPFLAAAircraftCategory(const GATAS::AircraftPositionInfo &position) const
{
    // clang-format off
    switch (position.aircraftType) {
        case GATAS::AircraftCategory::GLIDER: return "1";
        case GATAS::AircraftCategory::ROTORCRAFT:
        case GATAS::AircraftCategory::GYROCOPTER: return "3";
        case GATAS::AircraftCategory::SKY_DIVER: return "4";
        case GATAS::AircraftCategory::DROP_PLANE: return "5";
        case GATAS::AircraftCategory::HANG_GLIDER: return "6";
        case GATAS::AircraftCategory::PARA_GLIDER: return "7";
        case GATAS::AircraftCategory::LIGHT:
        case GATAS::AircraftCategory::SMALL:
        case GATAS::AircraftCategory::ULTRA_LIGHT_FIXED_WING:
        case GATAS::AircraftCategory::AEROBATIC: return "8"; // piston engines
        case GATAS::AircraftCategory::LARGE:
        case GATAS::AircraftCategory::HIGH_VORTEX:
        case GATAS::AircraftCategory::HEAVY_ICAO: return "9"; // turbo/jet
        // Set UNKNOWN to Airplane because we see a lot of MLAT aircraft without a time
        // Changing this to an aircraft at least does not make it look odd in an EFB
        case GATAS::AircraftCategory::UNKNOWN: return "8";
        case GATAS::AircraftCategory::LIGHT_THAN_AIR: return "B";
        case GATAS::AircraftCategory::UN_MANNED: return "D";
        case GATAS::AircraftCategory::POINT_OBSTACLE:
        case GATAS::AircraftCategory::LINE_OBSTACLE:
        case GATAS::AircraftCategory::CLUSTER_OBSTACLE: return "F";
        case GATAS::AircraftCategory::SURFACE_EMERGENCY_VEHICLE:
        case GATAS::AircraftCategory::SURFACE_VEHICLE:
        default: return "A"; // reserved
    }
    // clang-format on
}

/**
 * PFLAU – Heartbeat, status, and basic alarms
 *
 * Periodicity:
 * Sent once every second (1.8 s at maximum)
 * For skydemon see: http://forums.skydemon.aero/Topic32128.aspx
 */

void DataPort::sendPFLAU()
{
    GATAS::NMEAString pflau;
    etl::string_stream stream(pflau);
    const uint8_t txGpsStatus = hasGpsFix ? 1 : 0;

    // PFLAU,<RX>,<TX>,<GPS>,<Power>,<AlarmLevel>,<RelativeBearing>,<AlarmType>,<RelativeVertical>,<RelativeDistance>[,<ID>]
    // $PFLAU,5,1,1,1,0,,0,,,*
    stream << etl::make_string("$PFLAU,")
           << static_cast<uint8_t>(noTrackedAircraft) // Num RX
           << "," << static_cast<uint16_t>(txGpsStatus) // TX
           << "," << static_cast<uint16_t>(txGpsStatus) // GPS: 1 with fix, 0 without
           << etl::make_string(",1"  // Power 1 == OK
                               ",0"  // 0 Alarm level
                               ","   // RelativeBearing
                               ",0"  // AlarmType
                               ","   // RelativeVertical
                               ","   // RelativeDistance
                               ","); // Optional ID

    CoreUtils::addChecksumToNMEA(pflau);
    if (DEBUG_DATAPORT)
    {
        puts(pflau.c_str());
    }
    getBus().receive(GATAS::DataPortMsg{pflau});
    statistics.messages += 1;
}

/**
 * PFLAE – Self-test result and errors codes
 *
 * Periodicity:
 * Sent once after startup and completion of self-test, when errors occur, and when requested
 *
 */
void DataPort::sendPFLAE(const GATAS::AircraftPositionInfo &position)
{
    (void)position;
    // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
    GATAS::NMEAString nmeaString = "$PFLAE,A,0,0";

    CoreUtils::addChecksumToNMEA(nmeaString);
}

/**
 * PFLAV – Version information
 *
 * Periodicity:
 * Sent once after startup and completion of self-test and when requested
 *
 */
void DataPort::sendPFLAV(const GATAS::AircraftPositionInfo &position)
{
    (void)position;
    // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
    // GATAS::NMEAString nmeaString = "$PFLAV,A,0,0";
}

/**
 * PFLAR – Reset
 *
 * Periodicity:
 * Not applicable
 *
 */
void DataPort::sendPFLAR(const GATAS::AircraftPositionInfo &position)
{
    (void)position;
    // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
    //        GATAS::NMEAString nmeaString="$PFLAR,A,0,0";
}

/**
 * PFLAC – Device configuration
 *
 * Periodicity:
 * Sent when requested
 */

void DataPort::sendPFLAC(const GATAS::AircraftPositionInfo &position)
{
    (void)position;
    // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
    // GATAS::NMEAString nmeaString = "$PFLAC,HELLO,GLIDER_PILOTS";
}

/**
 * PFLAN – Continuous Analyzer of Radio Performance (CARP).
 *
 * Periodicity:
 * Sent when requested
 */
void DataPort::sendPFLANC(const GATAS::AircraftPositionInfo &position)
{
    (void)position;
    // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
    // GATAS::NMEAString nmeaString="$PFLANC,A,ERROR";
}

void DataPort::decimalDegreesToDMM(float degrees, int8_t &deg, float &min)
{
    // Convert decimal degrees to degrees and minutes
    deg = static_cast<int8_t>(degrees);
    min = (degrees - deg) * 60.0f;
}

/**
 * GPRMC – NMEA minimum recommended GPS navigation data
 */
// void DataPort::sendGPRMC(const GATAS::OwnshipPositionInfo &position)
// {
//     time_t timet;
//     struct tm *ptm;
//     time(&timet);
//     ptm = gmtime(&timet);
//     GATAS::NMEAString gnrmc;
//     etl::string_stream stream(gnrmc);

//     int8_t latDeg, lonDeg;
//     float latMin, lonMin;
//     decimalDegreesToDMM(position.lat, latDeg, latMin);
//     decimalDegreesToDMM(position.lon, lonDeg, lonMin);

//     // SkyDemon want's to see GPRMC, not GNRMC
//     stream << "$GPRMC,"
//            << etl::format_spec{}.width(2).fill('0') << ptm->tm_hour << ptm->tm_min << ptm->tm_sec << GATAS::RESET_FORMAT << "." << position.timestamp % 1000 << ","                              // @todo validate if position.timestamp%1000 is correct
//            << "A,"                                                                                                                                                                                 // validity - A-ok, V-invalid
//            << etl::format_spec{}.width(2).fill('0') << latDeg << etl::format_spec{}.precision(5).width(8).fill('0') << latMin << GATAS::RESET_FORMAT << "," << (position.lat >= 0 ? "N," : "S,") // Latitude
//            << etl::format_spec{}.width(3).fill('0') << lonDeg << etl::format_spec{}.precision(5).width(8).fill('0') << lonMin << GATAS::RESET_FORMAT << "," << (position.lon >= 0 ? "E," : "W,") // Longitude
//            << position.groundSpeed * MS_TO_KN << ","                                                                                                                                               // Speed over ground
//            << position.course << ","                                                                                                                                                               // Course over ground
//            << etl::format_spec{}.width(2).fill('0') << ptm->tm_mday << ptm->tm_mon + 1 << (ptm->tm_year + 1900 - 2000) << GATAS::RESET_FORMAT << ","                                             // Date
//            << ","                                                                                                                                                                                  // Magnetic variation
//            << ","                                                                                                                                                                                  // Magnetic variation direction
//            << "D";                                                                                                                                                                                 // A = Autonomous, D = DGPS, E =DR

//     CoreUtils::addChecksumToNMEA(gnrmc);
//     getBus().receive(GATAS::DataPortMsg{gnrmc});
//     statistics.messages += 1;
// }

// void DataPort::sendPGRMZ(const GATAS::OwnshipPositionInfo &position)
// {
//     //         // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
//     GATAS::NMEAString pgrmz;
//     etl::string_stream stream(pgrmz);

//     stream << "$PGRMZ,"
//            << (position.ellipseHeight - position.geoidSeparation) * M_TO_FT << "," // @todo: Not clear what altitude this really is
//            << "f,"                          // Altitude unit
//            << "3";                          // @todo Fix type 2=2D 3=3D

//     // example $PGRMZ,295,f,3*15
//     CoreUtils::addChecksumToNMEA(pgrmz);
//     getBus().receive(GATAS::DataPortMsg{pgrmz});
//     statistics.messages += 1;
// }

/**
 * GNGSA – GPS DOP and active satellites
 */
// void DataPort::sendGNGSA(const GATAS::AircraftPositionInfo &position)
// {
//     (void)position;
//     // (int32_t)round(relNorth),(int32_t)round(relEast),(int32_t)round(relVert),movePilotData->addressType, movePilotData->DevId.c_str(),(int32_t)round(movePilotData->heading),currentSpeed,movePilotData->climb,uint8_t(movePilotData->aircraftType));
//     // GATAS::NMEAString nmeaString="$PFLANC,A,ERROR";

//     GATAS::NMEAString gngsa{"$GNGSA,A,3,,,,,,,,,,,,,1.0,1.0,1.0"};
//     CoreUtils::addChecksumToNMEA(gngsa);
//     getBus().receive(GATAS::DataPortMsg{gngsa});
//     statistics.messages += 1;
// }

// void DataPort::sendGPGGA(const GATAS::OwnshipPositionInfo &position)
// {
//     (void)position;
//     time_t timet;
//     struct tm *ptm;
//     time(&timet);
//     ptm = gmtime(&timet);

//     GATAS::NMEAString gngga;
//     etl::string_stream stream(gngga);

//     int8_t latDeg, lonDeg;
//     float latMin, lonMin;
//     decimalDegreesToDMM(position.lat, latDeg, latMin);
//     decimalDegreesToDMM(position.lon, lonDeg, lonMin);

//     // SkyDemon want's to see GPGGA not GNGGA
//     stream << "$GPGGA,"
//            << etl::format_spec{}.width(2).fill('0') << ptm->tm_hour << ptm->tm_min << ptm->tm_sec << GATAS::RESET_FORMAT << "." << position.timestamp % 1000 << ","                              // @todo validate if position.timestamp%1000 is correct
//            << etl::format_spec{}.width(2).fill('0') << latDeg << etl::format_spec{}.precision(5).width(8).fill('0') << latMin << GATAS::RESET_FORMAT << "," << (position.lat >= 0 ? "N," : "S,") // Latitude
//            << etl::format_spec{}.width(3).fill('0') << lonDeg << etl::format_spec{}.precision(5).width(8).fill('0') << lonMin << GATAS::RESET_FORMAT << "," << (position.lon >= 0 ? "E," : "W,") // Longitude
//            << "2,"                                                                                                                                                                                 // 1==GPS, 2=DGPS, 9=DBAS
//            << "6,"                                                                                                                                                                                 // Number of satellites
//            << "1.0,"                                                                                                                                                                               // Horizontal dilution of position (hdop)
//            << (position.ellipseHeight - position.geoidSeparation) << ","                                                                                                                                                        // Altitude Wgs84
//            << "M,"                                                                                                                                                                                 // Altitude unit
//            << "position.geoidSeparation,"                                                                                                                                                                                 // Geoid Separation
//            << "M,"                                                                                                                                                                                 // Unit
//            << ","                                                                                                                                                                                  // age of differential GPS data
//            << "";                                                                                                                                                                                  // Differential reference station ID

//     CoreUtils::addChecksumToNMEA(gngga);
//     getBus().receive(GATAS::DataPortMsg{gngga});
//     statistics.messages += 1;
// }

/**
 * We seem to need this to get altitude in SkyDemon
 */
// void DataPort::sendGPGSA()
// {
//     GATAS::NMEAString gpgsa;
//     etl::string_stream stream(gpgsa);

//     stream << "$GPGSA,A,3,,,,,,,,,,,,,1.0,1.0,1.0";
//     CoreUtils::addChecksumToNMEA(gpgsa);
//     getBus().receive(GATAS::DataPortMsg{gpgsa});
//     statistics.messages += 1;
// }

void DataPort::sendLK8EX1()
{
    GATAS::NMEAString gpgsa;
    etl::string_stream stream(gpgsa);
    stream << etl::make_string("$LK8EX1,999999,999999,9999,99,999");
    CoreUtils::addChecksumToNMEA(gpgsa);
    getBus().receive(GATAS::DataPortMsg{gpgsa});
    statistics.messages += 1;
}

void DataPort::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"messages:k\":" << statistics.messages;
    stream << ",\"pflauEnabled:b\":" << pflauEnabled;
    stream << "}";
}

void DataPort::on_receive(const GATAS::WifiConnectionStateMsg &msg)
{
    gatasIp = msg.gatasIp;
}

void DataPort::on_receive(const GATAS::Every30SecMsg &msg)
{
    (void)msg;
    char ipStr[16];
    ip4_addr_t ip(gatasIp);
    ip4addr_ntoa_r(&ip, ipStr, sizeof(ipStr));

    GATAS::NMEAString infoMsg;
    etl::string_stream stream(infoMsg);
    stream << etl::make_string("$PGATS,") << etl::string_view(ipStr);
    CoreUtils::addChecksumToNMEA(infoMsg);
    getBus().receive(GATAS::DataPortMsg{infoMsg});
}
