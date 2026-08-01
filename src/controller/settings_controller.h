/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * Settings menu controller -- owns the Radio/Display menu item tables, every
 * cycle/toggle/set callback, and on_settings_changed() (applies a settings
 * change to hardware, refreshes the row's value_str buffer). app.c only
 * calls settings_controller_init() once at boot and wires
 * settings_controller_create_screen() into its frame_ops[] table.
 */

#ifndef DMRTASTIC_SETTINGS_CONTROLLER_H_
#define DMRTASTIC_SETTINGS_CONTROLLER_H_

#include <stdint.h>

#include <lvgl.h>

/** One Settings row: a label, the action its focus/click/encoder-turn runs (dir: +1
 * forward/CW, -1 backward/CCW), and its current value as display text (NULL if
 * action-only). Controller-shaped data -- screen_settings.c only renders it, and
 * the console's "settings" command renders the same tables as text.
 */
typedef struct {
	const char *label;
	void (*on_select)(int8_t dir);
	const char *value_str;
} menu_item_t;

/** Loads settings, subscribes this controller to changes, and applies the loaded/defaulted
 * state to hardware and the value_str buffers. Call once from app_init(), before
 * settings_controller_create_screen().
 */
void settings_controller_init(void);

/** Builds the Settings screen (screen_settings.c's tabview) wired to this controller's
 * radio_items[]/display_items[] tables.
 */
lv_obj_t *settings_controller_create_screen(lv_obj_t *parent);

/** Exposes the Radio/Display menu tables to other renderers (currently console_view.c's
 * "settings" command) without reaching into screen_settings.h, which is LVGL-internal.
 */
void settings_controller_get_radio_items(const menu_item_t **items, uint8_t *count);
void settings_controller_get_display_items(const menu_item_t **items, uint8_t *count);

/** Cycles the squelch preset table by dir (+1/-1). Shared by the Settings Radio tab's
 * "Squelch" row and the quick menu's "Squelch" row -- both surfaces drive the same
 * preset index, so they can never disagree on the current value.
 */
void settings_controller_cycle_squelch(int8_t dir);

/** Toggles 25K/12.5K bandwidth; dir is ignored (2-state toggle). Shared by the Settings
 * Radio tab's "Bandwidth" row and the quick menu's "Bandwidth" row.
 */
void settings_controller_cycle_bandwidth(int8_t dir);

/** Cycles Off -> the 50 CTCSS tones -> Off. Shared by the Settings Radio tab's
 * "CTCSS/DCS" row and the quick menu's "CTCSS/DCS" row.
 */
void settings_controller_cycle_css(int8_t dir);

#endif /* DMRTASTIC_SETTINGS_CONTROLLER_H_ */
