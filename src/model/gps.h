/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_GPS_H_
#define DMRTASTIC_GPS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gps_fix_state {
	GPS_FIX_NONE,      /* no NMEA sentence received yet */
	GPS_FIX_SEARCHING, /* sentences received, no valid position yet */
	GPS_FIX_ACQUIRED,  /* last sentence carried a valid position */
};

/** One-shot init: checks USART1 is ready and arms interrupt-driven RX into a ring buffer. */
int gps_init(void);

/** Self-throttled; safe to call every tick. Drains the RX ring buffer and parses complete
 * $GPRMC/$GPGGA (or $GN... talker-ID) sentences out of it.
 */
void gps_poll(void);

enum gps_fix_state gps_get_fix_state(void);

/** Last known position as signed x1e4-degree fixed point (matches console_format_latlon());
 * false if no fix has ever been acquired.
 */
bool gps_get_position(int32_t *lat_x10000, int32_t *lon_x10000);

/** Last known speed over ground in tenths of a knot, from $GPRMC; 0 if no fix yet. */
uint16_t gps_get_speed_knots_tenths(void);

/** Last known UTC time of day from the GPS; false if never received. */
bool gps_get_utc_time(uint8_t *hour, uint8_t *min, uint8_t *sec);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_GPS_H_ */
