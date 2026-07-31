// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Settings frame — persistent lv_tabview ("Radio"/"Display"/"Info"); two-level
 * nav, see screen_settings_in_rows_level(). Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_SETTINGS_H_
#define DMRTASTIC_UI_SCREEN_SETTINGS_H_

#include "app.h"
#include "controller/settings_controller.h" /* menu_item_t */

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

/** radio_items/display_items must stay valid for the program's lifetime (static arrays). */
lv_obj_t *screen_settings_create(lv_obj_t *parent,
				  const menu_item_t *radio_items, uint8_t radio_count,
				  const menu_item_t *display_items, uint8_t display_count);

/** Refreshes row value labels and the Info tab's uptime/RSSI text. */
void screen_settings_update(lv_obj_t *screen);

/** Handles ENCODER_CW/CCW by calling the focused row's on_select(±1); no-op at the tab level. */
void screen_settings_handle_action(lv_obj_t *screen, ui_action_t action);

/** True once Green has descended into a tab's row list. */
bool screen_settings_in_rows_level(void);

/** Ascends to the tab level: restores tab-button group membership and focus. No-op if already there. */
void screen_settings_exit_rows_level(void);

#endif /* DMRTASTIC_UI_SCREEN_SETTINGS_H_ */
