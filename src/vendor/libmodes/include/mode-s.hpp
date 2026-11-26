#ifndef __MODE_S_DECODER_H
#define __MODE_S_DECODER_H

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#define MODE_S_ICAO_CACHE_LEN 2 // Power of two required Note: GATAS doesn't use this
#define MODE_S_LONG_MSG_BYTES (112/8)
#define MODE_S_UNIT_FEET 0
#define MODE_S_UNIT_METERS 1
#define MODE_S_INVALID_GROUND_SPEED UINT16_MAX // TC19 ground vector unavailable
#define MODE_S_INVALID_ALTITUDE INT32_MIN // Unavailable or invalid DF17 AC12 altitude

// Program state
typedef struct {
  // Internal state
  uint32_t icao_cache[MODE_S_ICAO_CACHE_LEN*2]; // Recently seen ICAO addresses cache

  // Configuration
  uint8_t fix_errors; // Single bit error correction if true
  uint8_t aggressive; // Aggressive detection algorithm
  uint8_t check_crc;  // Only display messages with good CRC
} mode_s_t;

// The struct we use to store information about a decoded message
struct mode_s_msg {
  unsigned char msg[MODE_S_LONG_MSG_BYTES]; // Binary message
  // Generic fields
  uint8_t msgbits;                 // Number of bits in message
  uint8_t msgtype;                 // Downlink format #
  uint8_t crcok;                   // True if CRC was valid
  uint32_t crc;                    // Message CRC
  int8_t errorbit;                 // Bit corrected. -1 if no bit corrected.
  uint32_t aa;                     // ICAO Address bytes 1 2 and 3
  uint8_t phase_corrected;         // True if phase correction was applied.

  // DF 11
  uint8_t ca;                      // Responder capabilities.

  // DF 17
  uint8_t metype;                  // Extended squitter message type.
  uint8_t mesub;                   // Extended squitter message subtype.
  uint8_t heading_is_valid;
  int16_t heading;
  uint8_t aircraft_type;
  uint8_t fflag;                   // 1 = Odd, 0 = Even CPR message.
  uint8_t tflag;                   // UTC synchronized?
  int32_t raw_latitude;            // Non decoded latitude
  int32_t raw_longitude;           // Non decoded longitude
  char flight[9];                  // 8 chars flight number.
  uint8_t ew_dir;                  // 0 = East, 1 = West.
  int ew_velocity;                 // E/W velocity.
  uint8_t ns_dir;                  // 0 = North, 1 = South.
  int ns_velocity;                 // N/S velocity.
  uint8_t vert_rate_source;        // Vertical rate source.
  uint8_t vert_rate_sign;     // Vertical rate sign.
  int16_t vert_rate;               // Encoded TC19 magnitude: 0 unavailable, 1 level, then 64 ft/min per step.
  uint16_t velocity;               // Computed from EW and NS velocity.

  // DF4, DF5, DF20, DF21
  uint8_t fs;                     // Flight status for DF4,5,20,21
  uint8_t dr;                     // Request extraction of downlink request.
  uint8_t um;                     // Request extraction of downlink request.
  uint16_t identity;              // 13 bits identity (Squawk).

  // Fields used by multiple message types.
  int32_t altitude;
  uint8_t unit;
  int16_t gnssMinusBaroFeet;     // TC19 signed difference in feet, INT16_MIN when unavailable
};

typedef void (*mode_s_callback_t)(mode_s_t *self, struct mode_s_msg *mm);

void mode_s_init(mode_s_t *self);
void mode_s_compute_magnitude_vector(unsigned char *data, uint16_t *mag, uint32_t size);
void mode_s_detect(mode_s_t *self, uint16_t *mag, uint32_t maglen, mode_s_callback_t);
void mode_s_decode(mode_s_t *self, struct mode_s_msg *mm, const uint8_t *msg);
void mode_s_decode_phase1(mode_s_t *self, struct mode_s_msg *mm, const uint8_t *msg);
void mode_s_decode_phase2(mode_s_t *self, struct mode_s_msg *mm);
uint32_t mode_s_checksum(const uint8_t *msg, int bits);
extern const uint32_t mode_s_checksum_table[112];

#endif
