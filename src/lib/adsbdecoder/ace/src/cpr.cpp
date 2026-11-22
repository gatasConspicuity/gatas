/* Mode1090, a Mode S messages decoder for RTLSDR devices.
 *
 * Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
 * Copyright (C) 2023 by R. van Twisk <vantwisk@gmail.com>
 *
 * Modified by R. van Twisk to better support embedded platforms
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS int32_t ERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "../cpr.hpp"
#include <cmath>

int16_t  cprModint (int16_t  a, int16_t  b)
{
    int16_t  res = a % b;
    if (res < 0) res += b;
    return res;
}

/* Always positive MOD operation, used for CPR decoding. */
float cprModDouble(float a, float b)
{
    float res = fmodf(a, b);
    if (res < 0) res += b;
    return res;
}



/* The NL function uses the precomputed table from 1090-WP-9-14 */
int16_t  cprNLFunction(float lat)
{
    if (lat < 0.0f) lat = -lat; /* Table is simmetric about the equator. */
    if (lat < 10.47047130f) return 59;
    if (lat < 14.82817437f) return 58;
    if (lat < 18.18626357f) return 57;
    if (lat < 21.02939493f) return 56;
    if (lat < 23.54504487f) return 55;
    if (lat < 25.82924707f) return 54;
    if (lat < 27.93898710f) return 53;
    if (lat < 29.91135686f) return 52;
    if (lat < 31.77209708f) return 51;
    if (lat < 33.53993436f) return 50;
    if (lat < 35.22899598f) return 49;
    if (lat < 36.85025108f) return 48;
    if (lat < 38.41241892f) return 47;
    if (lat < 39.92256684f) return 46;
    if (lat < 41.38651832f) return 45;
    if (lat < 42.80914012f) return 44;
    if (lat < 44.19454951f) return 43;
    if (lat < 45.54626723f) return 42;
    if (lat < 46.86733252f) return 41;
    if (lat < 48.16039128f) return 40;
    if (lat < 49.42776439f) return 39;
    if (lat < 50.67150166f) return 38;
    if (lat < 51.89342469f) return 37;
    if (lat < 53.09516153f) return 36;
    if (lat < 54.27817472f) return 35;
    if (lat < 55.44378444f) return 34;
    if (lat < 56.59318756f) return 33;
    if (lat < 57.72747354f) return 32;
    if (lat < 58.84763776f) return 31;
    if (lat < 59.95459277f) return 30;
    if (lat < 61.04917774f) return 29;
    if (lat < 62.13216659f) return 28;
    if (lat < 63.20427479f) return 27;
    if (lat < 64.26616523f) return 26;
    if (lat < 65.31845310f) return 25;
    if (lat < 66.36171008f) return 24;
    if (lat < 67.39646774f) return 23;
    if (lat < 68.42322022f) return 22;
    if (lat < 69.44242631f) return 21;
    if (lat < 70.45451075f) return 20;
    if (lat < 71.45986473f) return 19;
    if (lat < 72.45884545f) return 18;
    if (lat < 73.45177442f) return 17;
    if (lat < 74.43893416f) return 16;
    if (lat < 75.42056257f) return 15;
    if (lat < 76.39684391f) return 14;
    if (lat < 77.36789461f) return 13;
    if (lat < 78.33374083f) return 12;
    if (lat < 79.29428225f) return 11;
    if (lat < 80.24923213f) return 10;
    if (lat < 81.19801349f) return 9;
    if (lat < 82.13956981f) return 8;
    if (lat < 83.07199445f) return 7;
    if (lat < 83.99173563f) return 6;
    if (lat < 84.89166191f) return 5;
    if (lat < 85.75541621f) return 4;
    if (lat < 86.53536998f) return 3;
    if (lat < 87.00000000f) return 2;
    else return 1;
}

int16_t  cprNFunction(float lat, bool  fflag)
{
    int16_t  nl = cprNLFunction(lat) - (fflag ? 1 : 0);
    if (nl < 1) nl = 1;
    return nl;
}

float cprDlonFunction(float lat, bool  fflag, bool surface=false)
{
    return (surface ? 90.0f : 360.0f) / cprNFunction(lat, fflag);
}


/* This algorithm comes from:
 * http://www.lll.lu/~edward/edward/adsb/DecodingADSBposition.html.
 *
 *
 * A few remarks:
 * 1) 131072 is 2^17 since CPR latitude and longitude are encoded in 17 bits.
 * 2) We assume that we always received the odd packet as last packet for
 *    simplicity. This may provide a position that is less fresh of a few
 *    seconds.
 */
etl::optional<CprPosition> decodeCPR(bool fflag, uint32_t even_cprlat, uint32_t even_cprlon,
                                     uint32_t odd_cprlat, uint32_t odd_cprlon)
{
    constexpr float AirDlat0 = 360.0f / 60;
    constexpr float AirDlat1 = 360.0f / 59;

    float lat0 = static_cast<float>(even_cprlat);
    float lat1 = static_cast<float>(odd_cprlat);
    float lon0 = static_cast<float>(even_cprlon);
    float lon1 = static_cast<float>(odd_cprlon);

    float rlat, rlon;

    /* Compute the Latitude Index "j" */
    int16_t  j = static_cast<int16_t>(floorf(((59*lat0 - 60.0f*lat1) / 131072) + 0.5f));
    float rlat0 = AirDlat0 * (cprModint (j,60) + lat0 / 131072.0f);
    float rlat1 = AirDlat1 * (cprModint (j,59) + lat1 / 131072.0f);

    if (rlat0 >= 270.0f) rlat0 -= 360.0f;
    if (rlat1 >= 270.0f) rlat1 -= 360.0f;

    // Check to see that the latitude is in range: -90 .. +90
    if (rlat0 < -90.0f || rlat0 > 90.0f || rlat1 < -90.0f || rlat1 > 90.0f)
    {
        return etl::nullopt;
    }

    float cprLat0 = cprNLFunction(rlat0);
    float cprLat1 = cprNLFunction(rlat1);

    /* Check that both are in the same latitude zone, or abort. */
    if (cprLat0 != cprLat1)
    {
        return etl::nullopt;
    }

    /* Compute ni and the longitude index m */
    if (fflag)
    {
        /* Use odd packet. */
        int16_t  ni = cprNFunction(rlat1,true);
        int16_t  m = static_cast<int16_t>(floorf((((lon0 * (cprLat1-1)) -  (lon1 * cprLat1)) / 131072.0f) + 0.5f));
        rlon = cprDlonFunction(rlat1,true) * (cprModint (m,ni)+lon1/131072);
        rlat = rlat1;

    }
    else
    {
        /* Use even packet. */
        int16_t  ni = cprNFunction(rlat0,false);
        int16_t  m = static_cast<int16_t>(floorf((((lon0 * (cprLat0-1)) -  (lon1 * cprLat0)) / 131072.0f) + 0.5f));
        rlon = cprDlonFunction(rlat0,false) * (cprModint (m,ni)+lon0/131072);
        rlat = rlat0;
    }
    rlon -= floorf( (rlon + 180.0f) / 360.0f ) * 360.0f;

    return CprPosition{rlat, rlon};
}
