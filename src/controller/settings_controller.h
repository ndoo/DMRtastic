// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Settings menu controller -- owns the Radio/Display menu item tables, every
 * cycle/toggle/set callback, and on_settings_changed() (applies a settings
 * change to hardware, refreshes the row's value_str buffer). app.c only
 * calls settings_controller_init() once at boot and wires
 * settings_controller_create_screen() into its frame_ops[] table.
 */

#ifndef DMRTASTIC_SETTINGS_CONTROLLER_H_
#define DMRTASTIC_SETTINGS_CONTROLLER_H_

#include <lvgl.h>

/** Loads settings, subscribes this controller to changes, and applies the loaded/defaulted
 * state to hardware and the value_str buffers. Call once from app_init(), before
 * settings_controller_create_screen(). */
void settings_controller_init(void);

/** Builds the Settings screen (screen_settings.c's tabview) wired to this controller's
 * radio_items[]/display_items[] tables. */
lv_obj_t *settings_controller_create_screen(lv_obj_t *parent);

#endif /* DMRTASTIC_SETTINGS_CONTROLLER_H_ */
