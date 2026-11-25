
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "pico/rand.h"
#include "pico/time.h"
#include "coreutils.hpp"


TEST_CASE( "msSinceEpoch", "[single-file]" )
{
    time_us_Value = 0;
    CoreUtils::setOffsetMsSinceEpoch(0);
    time_us_Value = 1000000;
    REQUIRE( CoreUtils::msSinceEpoch() == 1000 );

    time_us_Value = 1200'000;
    CoreUtils::setOffsetMsSinceEpoch(1210);
    REQUIRE( CoreUtils::msSinceEpoch() == 1210 );

    time_us_Value = 2500'000;
    REQUIRE( CoreUtils::msSinceEpoch() == 2510 );
}

TEST_CASE( "msInSecond", "[single-file]" )
{
    time_us_Value = 23456623;
    CoreUtils::setPPS(0);
    REQUIRE( CoreUtils::msInSecond() == 0 );

    time_us_Value = time_us_Value + 1758'000;
    REQUIRE( CoreUtils::msInSecond() == 758 );
}

TEST_CASE( "timeUs32 must be alliged with PPS", "[single-file]" )
{
    time_us_Value = 23'456'623;
    CoreUtils::setPPS(0);
    time_us_Value = time_us_Value+216500;
    REQUIRE( CoreUtils::timeUs32() == 23216500 );
}

TEST_CASE("setPPS handles signed software offsets", "[single-file]")
{
    time_us_Value = 23'000'100;
    CoreUtils::setPPS(250);
    REQUIRE(CoreUtils::timeUs32() == 23'000'250);
    REQUIRE(CoreUtils::timeUs64() == 23'000'250);
    REQUIRE(CoreUtils::monotonic() == 23'000'100);
    time_us_Value += 150;
    REQUIRE(CoreUtils::usInSecond() == 400);
    REQUIRE(CoreUtils::timeUs32() == 23'000'400);
    REQUIRE(CoreUtils::monotonic() == 23'000'250);

    time_us_Value = 23'000'250;
    CoreUtils::setPPS(-100);
    REQUIRE(CoreUtils::timeUs32() == 22'999'900);
    REQUIRE(CoreUtils::timeUs64() == 22'999'900);
    REQUIRE(CoreUtils::monotonic() == 23'000'250);
    REQUIRE(CoreUtils::usInSecond() == 999'900);
}

TEST_CASE( "usToReference must handle wraparounds", "[single-file]" )
{
    REQUIRE( CoreUtils::usToReference(1000, 750) == 250);
    REQUIRE( CoreUtils::usToReference(750, 10000) ==  -9250);

    REQUIRE( CoreUtils::usToReference(0xFFFFFF-250, 0xFFFFFF+500) == -750);
    REQUIRE( CoreUtils::usToReference(0xFFFFFF+250, 0xFFFFFF-750) == 1000);

    REQUIRE( CoreUtils::usToReference(0xFFFFFF-1000, 0xFFFFFF-750) == -250);
    REQUIRE( CoreUtils::usToReference(0xFFFFFF-750, 0xFFFFFF-1000) == 250);
}

TEST_CASE( "usToReference must handle true uint32_t rollover", "[single-file]" )
{
    constexpr uint32_t justBeforeWrap = UINT32_MAX - 250;
    constexpr uint32_t justAfterWrap = 500;

    // reference in the past across the 32-bit timer wrap
    REQUIRE( CoreUtils::usToReference(justBeforeWrap, justAfterWrap) == -751);

    // reference in the future across the 32-bit timer wrap
    REQUIRE( CoreUtils::usToReference(justAfterWrap, justBeforeWrap) == 751);
}

TEST_CASE( "usDiff must handle wraparounds", "[single-file]" )
{
    REQUIRE( CoreUtils::usDiff(1000, 750) ==  250);
    REQUIRE( CoreUtils::usDiff(750, 10000) ==  9250);

    REQUIRE( CoreUtils::usDiff(0xFFFFFF-250, 0xFFFFFF+500) == 750);
    REQUIRE( CoreUtils::usDiff(0xFFFFFF+250, 0xFFFFFF-750) == 1000);

    REQUIRE( CoreUtils::usDiff(0xFFFFFF-1000, 0xFFFFFF-750) == 250);
    REQUIRE( CoreUtils::usDiff(0xFFFFFF-750, 0xFFFFFF-1000) == 250);
}

TEST_CASE( "usDiff must handle true uint32_t rollover", "[single-file]" )
{
    constexpr uint32_t justBeforeWrap = UINT32_MAX - 250;
    constexpr uint32_t justAfterWrap = 500;

    REQUIRE( CoreUtils::usDiff(justBeforeWrap, justAfterWrap) == 751);
    REQUIRE( CoreUtils::usDiff(justAfterWrap, justBeforeWrap) == 751);
}

TEST_CASE( "isUsReached", "[single-file]" )
{
    time_us_Value = 0;
    CoreUtils::setPPS(0);
    REQUIRE( CoreUtils::isUsReached(10000) == false );

    time_us_Value = 10001;
    REQUIRE( CoreUtils::isUsReached(10000) == true );

    time_us_Value = 1000000;
    REQUIRE( CoreUtils::isUsReached(10000) == true );
}

TEST_CASE( "isUsReached must handle true uint32_t rollover", "[single-file]" )
{
    constexpr uint32_t justBeforeWrap = UINT32_MAX - 250;
    constexpr uint32_t justAfterWrap = 500;

    // Reference is still in the future across the 32-bit timer wrap.
    REQUIRE( CoreUtils::isUsReached(justAfterWrap, justBeforeWrap) == false );

    // Reference is already in the past across the 32-bit timer wrap.
    REQUIRE( CoreUtils::isUsReached(justBeforeWrap, justAfterWrap) == true );
}

TEST_CASE( "msDelayToReference", "[single-file]" )
{
    time_us_Value = 0;
    time_us_Value = 23456623;
    CoreUtils::setPPS(0);
    time_us_Value = time_us_Value + 313'000;
    REQUIRE( CoreUtils::msDelayToReference(411, 123) == 288 ); // 123ms into whole second

    REQUIRE( CoreUtils::msDelayToReference(123, 411) == 712 );  // 411ms into whole second

    REQUIRE( CoreUtils::msDelayToReference(200) == 887 );
}


TEST_CASE( "secondsSinceEpoch", "[single-file]" )
{
    time_us_Value = 0;
    CoreUtils::setOffsetMsSinceEpoch(1698800584010);
    REQUIRE( CoreUtils::secondsSinceEpoch() == 1698800584 );

    // ms is always round down by design so 999ms in second is still considered teh previous second
    CoreUtils::setOffsetMsSinceEpoch(1698800584510);
    REQUIRE( CoreUtils::secondsSinceEpoch() == 1698800584 );
}

TEST_CASE("timeUs32FromMsInMinute maps packet time to local PPS-aligned time", "[single-file]")
{
    time_us_Value = 70'321'000;
    CoreUtils::setPPS(321'000);
    CoreUtils::setOffsetMsSinceEpoch(43'210'321);

    auto timestampUs = CoreUtils::timeUs32FromMsInMinute(9'000);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 69'000'000);
    timestampUs = CoreUtils::timeUs32FromMsInMinute(10'000);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 70'000'000);
    timestampUs = CoreUtils::timeUs32FromMsInMinute(10'123);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 70'123'000);
    timestampUs = CoreUtils::timeUs32FromMsInMinute(5'000);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 65'000'000);

    REQUIRE_FALSE(CoreUtils::timeUs32FromMsInMinute(11'000).has_value());
    REQUIRE_FALSE(CoreUtils::timeUs32FromMsInMinute(55'000).has_value());
    timestampUs = CoreUtils::timeUs32FromMsInMinute(55'321, 15'000);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 55'321'000);
}

TEST_CASE("timeUs32FromMsInMinute handles minute rollover and rejects stale timestamps", "[single-file]")
{
    time_us_Value = 59'800'000;
    CoreUtils::setPPS(800'000);
    CoreUtils::setOffsetMsSinceEpoch(59'800);

    auto timestampUs = CoreUtils::timeUs32FromMsInMinute(59'000);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 59'000'000);
    timestampUs = CoreUtils::timeUs32FromMsInMinute(59'123);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 59'123'000);
    REQUIRE_FALSE(CoreUtils::timeUs32FromMsInMinute(0).has_value());
    timestampUs = CoreUtils::timeUs32FromMsInMinute(55'000);
    REQUIRE(timestampUs.has_value());
    REQUIRE(timestampUs.value() == 55'000'000);
    REQUIRE_FALSE(CoreUtils::timeUs32FromMsInMinute(48'000).has_value());
}

TEST_CASE( "distanceAccurate", "[single-file]" )
{
    using namespace Catch::literals;
    time_us_Value = 0;
    REQUIRE( CoreUtils::distanceAccurate(54, 4, 54, 4) == 0 );

    REQUIRE( CoreUtils::distanceAccurate(0, 4, 0, 5) == 111206_a );
    REQUIRE( CoreUtils::distanceAccurate(1, 4, 0, 4) == 111206_a );

    Catch::Approx target = Catch::Approx(23370).margin(18);
    for (int lat = -50; lat <= 50; lat=lat + 100)
    {
        for (int lon = -4; lon <= 4; lon=lon + 8)
        {
            REQUIRE( CoreUtils::distanceAccurate(lat+0.1, lon + .6, lat+.3, lon+.7) == target );
        }
    }
}

TEST_CASE( "distanceFast", "[single-file]" )
{
    using namespace Catch::literals;
    time_us_Value = 0;
    REQUIRE( CoreUtils::distanceFast(54, 4, 54, 4) == 0 );

    REQUIRE( CoreUtils::distanceFast(0, 4, 0, 5) == 111321_a );
    REQUIRE( CoreUtils::distanceFast(1, 4, 0, 4) == 111139_a );

    Catch::Approx target = Catch::Approx(23370).margin(24);
    for (int lat = -50; lat <= 50; lat=lat + 100)
    {
        for (int lon = -4; lon <= 4; lon=lon + 8)
        {
            REQUIRE( CoreUtils::distanceFast(lat+0.1, lon + .6, lat+.3, lon+.7) == target );
        }
    }
}

TEST_CASE("withinDistance: dateline crossing", "[geo]")
{
    float lat = 0.0f;
    float lon1 = 179.999f;
    float lon2 = -179.999f;

    // ~224 meters apart
    REQUIRE(CoreUtils::distanceFast(lat, lon1, lat, lon2) == Catch::Approx(224).margin(1));
}

TEST_CASE( "bearingFromInRad", "[single-file]" )
{
    using namespace Catch::literals;
    int lat, lon;

    lat=50;
    lon=4;
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon+1) * RADS_TO_DEG == 32.07481_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon+1) * RADS_TO_DEG == 146.6172_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon-1) * RADS_TO_DEG == 213.3828_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon-1) * RADS_TO_DEG == 327.9252_a );

    lat=-50;
    lon=4;
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon+1) * RADS_TO_DEG == 33.38282_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon+1) * RADS_TO_DEG == 147.9252_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon-1) * RADS_TO_DEG == 212.0748_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon-1) * RADS_TO_DEG == 326.6171_a );

    lat=50;
    lon=-4;
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon+1) * RADS_TO_DEG == 32.07481_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon+1) * RADS_TO_DEG == 146.6172_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon-1) * RADS_TO_DEG == 213.3828_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon-1) * RADS_TO_DEG == 327.9252_a );

    lat=-50;
    lon=-4;
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon+1) * RADS_TO_DEG == 33.38282_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon+1) * RADS_TO_DEG == 147.9252_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat-1, lon-1) * RADS_TO_DEG == 212.0748_a );
    REQUIRE( CoreUtils::bearingFromInRad( lat,  lon, lat+1, lon-1) * RADS_TO_DEG == 326.6171_a );
}

TEST_CASE( "getDistanceRelNorthRelEastFloat", "[single-file]" )
{
    using namespace Catch::literals;
    time_us_Value = 0;

    int lat, lon;

    lat=50;
    lon=4;
    auto result = CoreUtils::getDistanceRelNorthRelEastFloat( lat,  lon, lat+0.1, lon+0.1);
    REQUIRE( result.relNorthMeter == 11113.73047_a );
    REQUIRE( result.relEastMeter == 7155.56982_a );

    lat=50;
    lon=4;
    result = CoreUtils::getDistanceRelNorthRelEastFloat( lat,  lon, lat-0.1, lon-0.1);
    REQUIRE( result.relNorthMeter == -11113.73047_a );
    REQUIRE( result.relEastMeter == -7155.5698_a );
}

TEST_CASE( "getRadialSection", "[single-file]" )
{
    REQUIRE( CoreUtils::getRadialSection<8>(10) == 0 );
    REQUIRE( CoreUtils::getRadialSection<8>(180) == 4 );
    REQUIRE( CoreUtils::getRadialSection<8>(179) == 4 );
    REQUIRE( CoreUtils::getRadialSection<8>(181) == 4 );
}

TEST_CASE( "getPath", "[single-file]" )
{
    etl::string<32> pathToParse = "/foo/bar/bash.sh";
    auto path = CoreUtils::parsePath(pathToParse);
    REQUIRE( path.size() == 4 );
    REQUIRE( path.back() == "sh" );
    REQUIRE( path[0] == "foo" );
    REQUIRE( path[1] == "bar" );
    REQUIRE( path[2] == "bash" );
}

TEST_CASE( "addChecksumToNMEA", "[single-file]" )
{
    // Empty string stays empty
    GATAS::NMEAString pflau="$";
    CoreUtils::addChecksumToNMEA(pflau);
    REQUIRE( pflau == "$*00\r\n" );

    // Should start from 1 because string must contain the $
    pflau="1234";
    CoreUtils::addChecksumToNMEA(pflau);
    REQUIRE( pflau.size() == 9  );
    REQUIRE( pflau == "1234*35\r\n" );

    // Add checksum
    pflau="$1234";
    CoreUtils::addChecksumToNMEA(pflau);
    REQUIRE( pflau.size() == 10  );
    REQUIRE( pflau == "$1234*04\r\n" );

    // Add checksum
    pflau="$1234*";
    CoreUtils::addChecksumToNMEA(pflau);
    REQUIRE( pflau.size() == 10  );
    REQUIRE( pflau == "$1234*04\r\n" );

    // Replace existing checksum
    pflau="$1234*35";
    CoreUtils::addChecksumToNMEA(pflau);
    REQUIRE( pflau.size() == 10  );
    REQUIRE( pflau == "$1234*04\r\n" );
}

TEST_CASE("addChecksumToNMEA without CRLF", "[single-file]")
{
    GATAS::NMEAString sentence = "$GPRMC,183205.50,A,5253.55446,N,00444.17388,E,0.000,0.00,260826,,";
    CoreUtils::addChecksumToNMEA(sentence, false);

    REQUIRE(sentence.find('*') == sentence.size() - 3);
    REQUIRE_FALSE(sentence.ends_with("\r\n"));
    REQUIRE(CoreUtils::validateNMEAChecksum(sentence));
}

TEST_CASE( "createNmeaChecksum", "[single-file]" )
{
    // Empty string stays empty
    auto checked = CoreUtils::createNmeaChecksum("$abds,2,3,4");
    REQUIRE( checked == "$abds,2,3,4*0D\r\n" );
}

TEST_CASE( "hexStrToByteArray overflow", "[single-file]" )
{
    uint8_t bytes[7];
    bytes[6] = 0x12;
    CoreUtils::hexStrToByteArray("8D00FF4D2D58AA", sizeof(bytes) * 2 - 2, bytes);
    REQUIRE( bytes[0] == 0x8D );
    REQUIRE( bytes[1] == 0x00 );
    REQUIRE( bytes[2] == 0xFF );
    REQUIRE( bytes[3] == 0x4D );
    REQUIRE( bytes[4] == 0x2D );
    REQUIRE( bytes[5] == 0x58 );
    REQUIRE( bytes[6] == 0x12 ); // should stay the same and not turn into AA
}

TEST_CASE("egmGeoidOffset Example case (51, 4)", "[altitude]") {

    float result = CoreUtils::egmGeoidOffset(51, 4);

    REQUIRE(result  == 45);
}

TEST_CASE("egmGeoidOffset StaticGPS default position", "[altitude]")
{
    const float result = CoreUtils::egmGeoidOffset(51.8925725F, 4.0362312F);

    REQUIRE(result == 44.0F);
}

TEST_CASE("egmGeoidOffset Example case (72, -15.5)", "[altitude]") {

    float result = CoreUtils::egmGeoidOffset(72, -15.5);

    REQUIRE(result  == 55);
}

TEST_CASE("egmGeoidOffset Example case (-86, 4) outside of grid", "[altitude]") {

    float result = CoreUtils::egmGeoidOffset(-86, 4);

    REQUIRE(result  == 0);
}

TEST_CASE("northEastDistance", "[single-file]")
{
    using namespace Catch::literals;
    float lat, lon;

    lat=50.0;
    lon=4.0;
    auto result = CoreUtils::northEastDistance( lat,  lon, lat+0.1, lon+0.1);
    REQUIRE( result.east == 7155.56982_a );
    REQUIRE( result.north ==  11113.73047_a );

    result = CoreUtils::northEastDistance( lat,  lon, lat-0.1, lon-0.1);
    REQUIRE( result.east == -7155.5698_a );
    REQUIRE( result.north == -11113.73047_a );
}


TEST_CASE("isReachedS checks second deadlines across rollover", "[single-file]")
{
    REQUIRE_FALSE(CoreUtils::isReachedS(10, 9));
    REQUIRE(CoreUtils::isReachedS(10, 10));
    REQUIRE(CoreUtils::isReachedS(10, 11));
    REQUIRE_FALSE(CoreUtils::isReachedS(2, UINT32_MAX));
    REQUIRE(CoreUtils::isReachedS(UINT32_MAX, 2));
}
