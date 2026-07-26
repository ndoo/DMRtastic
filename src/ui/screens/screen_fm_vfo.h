// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * FM VFO operating screen — the default screen after boot.
 *
 * Layout (160 × 114 px content area):
 *   Frequency label (Montserrat 20): "146.525 000  MHz"   — top-centre
 *   RSSI bar (lv_bar):               90 % width           — middle
 *   Detail row (Montserrat 10):      "SQ:███░░  BW:25K  [OPEN]"  — bottom
 *
 * Data sources:
 *   Frequency  — read from AT1846S driver data at create time; updated
 *                only when value changes (cached to avoid redundant redraws)
 *   RSSI bar   — polled from AT1846S get_rssi() in update(), 200 ms period
 *   Squelch    — derived from RSSI noise byte in update()
 *   Bandwidth  — read from AT1846S driver at create time
 *
 * Actions handled: none yet (keypad not wired up).
 *
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_FM_VFO_H_
#define DMRTASTIC_UI_SCREEN_FM_VFO_H_

#include <lvgl.h>

lv_obj_t *screen_fm_vfo_create(lv_obj_t *parent);
void      screen_fm_vfo_destroy(lv_obj_t *screen);
void      screen_fm_vfo_update(lv_obj_t *screen);

#endif /* DMRTASTIC_UI_SCREEN_FM_VFO_H_ */
