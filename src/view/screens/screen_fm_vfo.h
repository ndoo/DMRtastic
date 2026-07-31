// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * FM VFO operating screen — the default screen after boot; see screen_fm_vfo_step().
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_FM_VFO_H_
#define DMRTASTIC_UI_SCREEN_FM_VFO_H_

#include <lvgl.h>

/** Builds the FM VFO screen and populates the initial frequency from the driver. */
lv_obj_t *screen_fm_vfo_create(lv_obj_t *parent);
void      screen_fm_vfo_destroy(lv_obj_t *screen);

/** Polls RSSI/squelch and drives the debounced retune state machine. */
void      screen_fm_vfo_update(lv_obj_t *screen);

/** Steps the shown RX frequency by settings_get_vfo_step_hz(); actual retune is debounced in update(). */
void      screen_fm_vfo_step(lv_obj_t *screen, bool up);

/** Appends a digit (0-9) to the in-progress direct-frequency entry. */
void      screen_fm_vfo_entry_digit(lv_obj_t *screen, int digit);

/** Removes the most recently entered digit of the in-progress entry. */
void      screen_fm_vfo_entry_backspace(lv_obj_t *screen);

/** Cancels the in-progress direct-frequency entry without committing it. */
void      screen_fm_vfo_entry_cancel(lv_obj_t *screen);

#endif /* DMRTASTIC_UI_SCREEN_FM_VFO_H_ */
