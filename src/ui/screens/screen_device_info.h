// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Device info screen — firmware version, uptime, and raw RSSI values.
 *
 * Useful for diagnostics during radio bring-up.
 *
 * Layout (Montserrat 10, 4 lines):
 *   Line 1: "DMRtastic v0.1.0-dev"
 *   Line 2: "Zephyr <kernel version>"
 *   Line 3: "Uptime: <N> s"         — refreshed each update()
 *   Line 4: "RSSI sig=<N> noise=<N>"— refreshed each update()
 *
 * Data sources:
 *   Version  — compile-time constants
 *   Uptime   — k_uptime_get()
 *   RSSI     — AT1846S get_rssi()
 *
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_DEVICE_INFO_H_
#define DMRTASTIC_UI_SCREEN_DEVICE_INFO_H_

#include <lvgl.h>

lv_obj_t *screen_device_info_create(lv_obj_t *parent);
void      screen_device_info_destroy(lv_obj_t *screen);
void      screen_device_info_update(lv_obj_t *screen);

#endif /* DMRTASTIC_UI_SCREEN_DEVICE_INFO_H_ */
