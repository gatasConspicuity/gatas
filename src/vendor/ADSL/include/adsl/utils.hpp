#pragma once

#include <cmath>
#include <stdint.h>
#include <inttypes.h>

#include <etl/algorithm.h>
#include <etl/math.h>
#include <etl/power.h>
#include <etl/ratio.h>
#include <etl/memory.h>
#include <etl/span.h>

namespace ADSL
{
  constexpr float DEG_TO_RADS = M_PI / 180.f; // degrees to radians

  template <class Type, int Bits>
  Type UnsVRdecode(Type Value)
  {
    const Type Thres = 1 << Bits;
    uint8_t Range = Value >> Bits;
    Value &= Thres - 1;
    if (Range == 0)
      return Value;
    if (Range == 1)
      return Thres + (Value << 1);
    if (Range == 2)
      return 3 * Thres + (Value << 2);
    return 7 * Thres + (Value << 3);
  }

  template <class Type, int Bits>
  Type UnsVRencode(Type Value)
  {
    const Type Thres = 1 << Bits;
    if (Value < Thres)
      return Value;
    if (Value < 3 * Thres)
      return Thres | ((Value - Thres) >> 1);
    if (Value < 7 * Thres)
      return 2 * Thres | ((Value - 3 * Thres) >> 2);
    if (Value < 15 * Thres)
      return 3 * Thres | ((Value - 7 * Thres) >> 3);
    return 4 * Thres - 1;
  }

  template <class Type, int Bits>
  Type SignVRencode(Type Value)
  {
    const Type SignMask = 1 << (Bits + 2);
    Type Sign = 0;
    if (Value < 0)
    {
      Value = (-Value);
      Sign = SignMask;
    }
    Value = UnsVRencode<Type, Bits>(Value);
    return Value | Sign;
  }

  template <class Type, int Bits>
  Type SignVRdecode(Type Value)
  {
    const Type SignMask = 1 << (Bits + 2);
    Type Sign = Value & SignMask;
    Value = UnsVRdecode<Type, Bits>(Value & (SignMask - 1));
    return Sign ? -Value : Value;
  }

  static constexpr float FNTtoFloatConf = 90.0007295677 / 0x40000000;

  inline float FNTtoFloat(int32_t Coord)
  {
    return FNTtoFloatConf * Coord;
  }

  inline int32_t FloatToFNT(float Coord)
  {
    return Coord / FNTtoFloatConf;
  }

  inline float wrapLonDelta(float dLon)
  {
    // Wrap to [-180, +180]
    if (dLon > 180.0f)
    {
      dLon -= 360.0f;
    }
    if (dLon < -180.0f)
    {
      dLon += 360.0f;
    }
    return dLon;
  }

  struct relNorthRelEast
  {
    float north;
    float east;
  };

  inline relNorthRelEast northEastDistance(float fromLat, float fromLon, float toLat,
                                           float toLon)
  {

    float dLat = toLat - fromLat;
    float dLon = toLon - fromLon;

    float kx = cosf(fromLat * DEG_TO_RADS) * 111321.0f;

    float dx = wrapLonDelta(dLon) * kx;
    float dy = dLat * 111139.0f;

    return {dy, dx};
  }

  /**
   * Calculate the distance between two points on earth fast
   *
   * Note: Distance in what we are interested in <30Km at lat 30 degrees the
   * difference is about 15m
   *
   * returns distance in meters
   * https://jamesloper.com/fastest-way-to-calculate-distance-between-two-coordinates
   * https://www.movable-type.co.uk/scripts/latlong.html
   */
  inline float distanceFast(float fromLat, float fromLon, float toLat, float toLon)
  {
    auto ne = northEastDistance(fromLat, fromLon, toLat, toLon);
    return sqrtf((ne.north * ne.north) + (ne.east * ne.east));
  }

  inline relNorthRelEast velocityFromTrack(float trackDeg, float speed)
  {
    float tr = trackDeg * DEG_TO_RADS;

    return {.north = cosf(tr) * speed, .east = sinf(tr) * speed};
  }

  /**
   * Compute TCPA (Closest Point of Approach) in seconds.
   */
  inline float computeTCPA(relNorthRelEast relativeDistance, float ownTrackDeg,
                           float ownSpeed, float tgtTrackDeg, float tgtSpeed)
  {

    // Velocities
    relNorthRelEast vOwn = velocityFromTrack(ownTrackDeg, ownSpeed);
    relNorthRelEast vTgt = velocityFromTrack(tgtTrackDeg, tgtSpeed);

    // Relative velocity
    relNorthRelEast v;
    v.east = vTgt.east - vOwn.east;
    v.north = vTgt.north - vOwn.north;

    float v2 = v.east * v.east + v.north * v.north;

    // If relative speed is basically zero → no CPA
    if (v2 < 0.1f)
    {
      return INFINITY;
    }

    // tcpa = - (r · v) / |v|²
    float dot = relativeDistance.east * v.east + relativeDistance.north * v.north;
    float tcpa = -dot / v2;

    if (tcpa < 0)
    {
      return INFINITY;
    }
    return tcpa;
  }

  inline bool isUsReached(uint32_t referenceUs, uint32_t now)
  {
    return static_cast<int32_t>(referenceUs - now) < 0;
  }

  inline uint32_t XXTEA_MX_KEY0(uint32_t Y, uint32_t Z, uint32_t Sum)
  {
    return ((((Z >> 5) ^ (Y << 2)) + ((Y >> 3) ^ (Z << 4))) ^ ((Sum ^ Y) + Z));
  }

  constexpr uint32_t XXTEADELTA = 0x9e3779b9;
  inline void XXTEA_Encrypt_Key0(uint32_t Data[], uint8_t Words, uint8_t Loops)
  {
    uint32_t Sum = 0;
    uint32_t Z = Data[Words - 1];
    uint32_t Y;
    for (; Loops; Loops--)
    {
      Sum += XXTEADELTA;
      for (uint8_t P = 0; P < (Words - 1); P++)
      {
        Y = Data[P + 1];
        Z = Data[P] += XXTEA_MX_KEY0(Y, Z, Sum);
      }
      Y = Data[0];
      Z = Data[Words - 1] += XXTEA_MX_KEY0(Y, Z, Sum);
    }
  }

  inline void XXTEA_Decrypt_Key0(uint32_t Data[], uint8_t Words, uint8_t Loops)
  {
    uint32_t Sum = Loops * XXTEADELTA;
    uint32_t Y = Data[0];
    uint32_t Z;
    for (; Loops; Loops--)
    {
      for (uint8_t P = Words - 1; P; P--)
      {
        Z = Data[P - 1];
        Y = Data[P] -= XXTEA_MX_KEY0(Y, Z, Sum);
      }
      Z = Data[Words - 1];
      Y = Data[0] -= XXTEA_MX_KEY0(Y, Z, Sum);
      Sum -= XXTEADELTA;
    }
  }

  template <typename T>
  inline void printBufferHex(etl::span<T> buffer)
  {

    printf("Length(%d) ", static_cast<int>(buffer.size()));

    for (size_t i = 0; i < buffer.size(); ++i)
    {
      if constexpr (sizeof(T) == 1)
        printf("0x%02" PRIX8, static_cast<uint8_t>(buffer[i]));
      else if constexpr (sizeof(T) == 2)
        printf("0x%04" PRIX16, static_cast<uint16_t>(buffer[i]));
      else if constexpr (sizeof(T) == 4)
        printf("0x%08" PRIX32, static_cast<uint32_t>(buffer[i]));
      else
        printf("0x%X", static_cast<unsigned int>(buffer[i])); // fallback
      if (i + 1 < buffer.size())
        printf(", ");
    }
  }

} // namespace ADSL
