/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_RADIO_DEBUG_H_
#define DMRTASTIC_RADIO_DEBUG_H_

#include <stdint.h>
#include <zephyr/drivers/rtc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raw register/RTC access for the debug console -- thin 1:1 driver wrappers, same
 * shape as radio_state.c but for the register-level surface instead of the
 * business-level one. This is the second (and only other) file allowed to call
 * DEVICE_DT_GET() on the at1846s/hr_c6000/rtc nodes; see ui_architecture.md.
 */

int radio_debug_at_read(uint8_t reg, uint8_t *hi, uint8_t *lo);
int radio_debug_at_write(uint8_t reg, uint8_t hi, uint8_t lo);

int radio_debug_bb_read(uint8_t page, uint8_t reg, uint8_t *val);
int radio_debug_bb_write(uint8_t page, uint8_t reg, uint8_t val);

/* AT1846S reg 0x1B raw pair: hi = noise/quieting indicator, lo = signal indicator. */
int radio_debug_rssi_raw(uint8_t *noise, uint8_t *signal);

int radio_debug_rtc_get(struct rtc_time *tm);
int radio_debug_rtc_set(int year, int mon, int day, int hour, int min, int sec);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_RADIO_DEBUG_H_ */
