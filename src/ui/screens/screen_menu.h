// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Generic scrollable list menu — reused by all settings screens.
 *
 * Usage pattern:
 *   1. Call screen_menu_prepare(title, items, count) with a static items array.
 *   2. Call ui_push_screen(SCREEN_MENU_*).
 *   3. The create function picks up the prepared data.
 *
 * Each row shows label on the left and value_str on the right (if non-NULL).
 * on_select() is invoked when the row is confirmed (OK key or tap).
 * Visible row height is 16 px; ~7 rows fit in the 114 px content area.
 *
 * Scroll position is preserved across update() calls but reset on destroy().
 *
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_MENU_H_
#define DMRTASTIC_UI_SCREEN_MENU_H_

#include <lvgl.h>
#include <stdint.h>

#include "../ui.h"

typedef struct {
	const char *label;
	void (*on_select)(void);   /* push child screen or toggle value */
	const char *value_str;     /* right-aligned; NULL if action-only */
} menu_item_t;

/*
 * Must be called before ui_push_screen(SCREEN_MENU_*).
 * items must remain valid for the lifetime of the screen (use static arrays).
 */
void screen_menu_prepare(const char *title, const menu_item_t *items,
			 uint8_t count);

lv_obj_t *screen_menu_create(lv_obj_t *parent);
void      screen_menu_destroy(lv_obj_t *screen);
void      screen_menu_update(lv_obj_t *screen);
void      screen_menu_handle_action(lv_obj_t *screen, ui_action_t action);

#endif /* DMRTASTIC_UI_SCREEN_MENU_H_ */
