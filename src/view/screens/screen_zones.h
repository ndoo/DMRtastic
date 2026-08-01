/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * Zone list screen — render-only view over zone_controller, same relationship
 * screen_fm_channel.c has to channel_controller. Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_ZONES_H_
#define DMRTASTIC_UI_SCREEN_ZONES_H_

#include <lvgl.h>

/** Builds the Zones screen and paints the current zone (zone_controller's cache as of the
 * last init()/step()/select()).
 */
lv_obj_t *screen_zones_create(lv_obj_t *parent);
void screen_zones_destroy(lv_obj_t *screen);

/** Repaints from zone_controller's current zone; no-op if the slot hasn't changed since the
 * last paint (covers console-driven zone_controller_step() calls too, same reasoning as
 * screen_fm_channel_update()).
 */
void screen_zones_update(lv_obj_t *screen);

/** Forwards a step to the controller; see zone_controller_step(). */
void screen_zones_step(lv_obj_t *screen, bool up);

#endif /* DMRTASTIC_UI_SCREEN_ZONES_H_ */
