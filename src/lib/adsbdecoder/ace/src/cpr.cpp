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
#include <etl/algorithm.h>
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



/* The NL function uses the precomputed table from 1090-WP-9-14. */
int16_t cprNLFunction(float lat)
{
    static constexpr float thresholds[] =
    {
        10.47047130f, 14.82817437f, 18.18626357f, 21.02939493f,
        23.54504487f, 25.82924707f, 27.93898710f, 29.91135686f,
        31.77209708f, 33.53993436f, 35.22899598f, 36.85025108f,
        38.41241892f, 39.92256684f, 41.38651832f, 42.80914012f,
        44.19454951f, 45.54626723f, 46.86733252f, 48.16039128f,
        49.42776439f, 50.67150166f, 51.89342469f, 53.09516153f,
        54.27817472f, 55.44378444f, 56.59318756f, 57.72747354f,
        58.84763776f, 59.95459277f, 61.04917774f, 62.13216659f,
        63.20427479f, 64.26616523f, 65.31845310f, 66.36171008f,
        67.39646774f, 68.42322022f, 69.44242631f, 70.45451075f,
        71.45986473f, 72.45884545f, 73.45177442f, 74.43893416f,
        75.42056257f, 76.39684391f, 77.36789461f, 78.33374083f,
        79.29428225f, 80.24923213f, 81.19801349f, 82.13956981f,
        83.07199445f, 83.99173563f, 84.89166191f, 85.75541621f,
        86.53536998f, 87.00000000f
    };

    if (lat < 0.0f)
    {
        lat = -lat;
    }

    const auto* boundary = etl::upper_bound(
        thresholds,
        thresholds + (sizeof(thresholds) / sizeof(thresholds[0])),
        lat);

    return static_cast<int16_t>(59 - (boundary - thresholds));
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

    int16_t cprLat0 = cprNLFunction(rlat0);
    int16_t cprLat1 = cprNLFunction(rlat1);

    /* Check that both are in the same latitude zone, or abort. */
    if (cprLat0 != cprLat1)
    {
        return etl::nullopt;
    }

    /* Compute ni and the longitude index m */
    if (fflag)
    {
        /* Use odd packet. */
        int16_t ni = cprLat1 - 1;
        if (ni < 1)
        {
            ni = 1;
        }
        int16_t  m = static_cast<int16_t>(floorf((((lon0 * (cprLat1-1)) -  (lon1 * cprLat1)) / 131072.0f) + 0.5f));
        rlon = (360.0f / ni) * (cprModint (m,ni)+lon1/131072);
        rlat = rlat1;

    }
    else
    {
        /* Use even packet. */
        int16_t ni = cprLat0;
        int16_t  m = static_cast<int16_t>(floorf((((lon0 * (cprLat0-1)) -  (lon1 * cprLat0)) / 131072.0f) + 0.5f));
        rlon = (360.0f / ni) * (cprModint (m,ni)+lon0/131072);
        rlat = rlat0;
    }
    rlon -= floorf( (rlon + 180.0f) / 360.0f ) * 360.0f;

    return CprPosition{rlat, rlon};
}
