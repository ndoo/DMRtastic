// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * FM Channel operating screen — render-only view over channel_controller, same
 * relationship screen_fm_vfo.c has to fm_vfo_controller. Internal header — include only
 * from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_FM_CHANNEL_H_
#define DMRTASTIC_UI_SCREEN_FM_CHANNEL_H_

#include <lvgl.h>

/** Builds the FM Channel screen and paints the current codeplug channel. */
lv_obj_t *screen_fm_channel_create(lv_obj_t *parent);
void      screen_fm_channel_destroy(lv_obj_t *screen);

/** Repaints from channel_controller's current channel; no-op if the index hasn't changed
 * since the last paint (covers console-driven channel_controller_step() calls too, since
 * the comparison is against live controller state, not a screen-local step count). */
void      screen_fm_channel_update(lv_obj_t *screen);

/** Forwards a step to the controller; see channel_controller_step(). */
void      screen_fm_channel_step(lv_obj_t *screen, bool up);

#endif /* DMRTASTIC_UI_SCREEN_FM_CHANNEL_H_ */
