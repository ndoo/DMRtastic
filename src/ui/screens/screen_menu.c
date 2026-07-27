// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "screen_menu.h"
#include "../theme.h"
#include "../ui.h"

#include <lvgl.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define ROW_H      16
#define ROW_PAD_X   4

#define SCREEN_MENU_MAX_ROWS 8

#define ROW_COLOR_NORMAL    (theme_colors()->bg)
#define ROW_COLOR_SELECTED  (theme_colors()->selection_bg)

/* Module-level state set by screen_menu_prepare() before create(). */
static const char       *s_title;
static const menu_item_t *s_items;
static uint8_t            s_count;

/* Row widgets and selection cursor, valid for the lifetime of the screen. */
static lv_obj_t *s_rows[SCREEN_MENU_MAX_ROWS];
static int8_t     s_sel;

/* Event callback for row buttons. */
static void row_cb(lv_event_t *e)
{
	const menu_item_t *item = lv_event_get_user_data(e);

	if (item && item->on_select) {
		item->on_select();
	}
}

void screen_menu_prepare(const char *title, const menu_item_t *items,
			 uint8_t count)
{
	s_title = title;
	s_items = items;
	s_count = count;
	s_sel = 0;
}

static void highlight_row(int8_t idx, bool on)
{
	if (idx < 0 || idx >= s_count || s_rows[idx] == NULL) {
		return;
	}
	lv_obj_set_style_bg_color(s_rows[idx],
				  on ? ROW_COLOR_SELECTED : ROW_COLOR_NORMAL,
				  LV_PART_MAIN);
}

lv_obj_t *screen_menu_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	/* Title bar */
	lv_obj_t *title_label = lv_label_create(scr);
	lv_label_set_text(title_label, s_title ? s_title : "MENU");
	lv_obj_set_style_text_color(title_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 2);

	/* Divider line under title */
	lv_obj_t *div = lv_obj_create(scr);
	lv_obj_set_size(div, lv_pct(100), 1);
	lv_obj_set_style_bg_color(div, theme_colors()->border, LV_PART_MAIN);
	lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
	lv_obj_align(div, LV_ALIGN_TOP_MID, 0, ROW_H + 2);

	/* Scrollable list container below title */
	lv_obj_t *list = lv_obj_create(scr);
	lv_obj_set_size(list, lv_pct(100),
			lv_obj_get_height(parent) - ROW_H - 4);
	lv_obj_align(list, LV_ALIGN_TOP_MID, 0, ROW_H + 4);
	lv_obj_set_style_bg_color(list, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
	lv_obj_set_layout(list, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

	uint8_t row_count = s_count;

	if (row_count > SCREEN_MENU_MAX_ROWS) {
		LOG_WRN("menu has %u rows, truncating to %u", row_count, SCREEN_MENU_MAX_ROWS);
		row_count = SCREEN_MENU_MAX_ROWS;
	}

	for (uint8_t i = 0; i < row_count; i++) {
		const menu_item_t *item = &s_items[i];

		lv_obj_t *row = lv_obj_create(list);
		lv_obj_set_size(row, lv_pct(100), ROW_H);
		lv_obj_set_style_bg_color(row, ROW_COLOR_NORMAL, LV_PART_MAIN);
		lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
		lv_obj_set_style_pad_hor(row, ROW_PAD_X, LV_PART_MAIN);
		lv_obj_set_style_pad_ver(row, 0, LV_PART_MAIN);
		lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		/* Cast away const: user_data is read-only in the callback. */
		lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED,
				    (void *)item);

		lv_obj_t *lbl = lv_label_create(row);
		lv_label_set_text(lbl, item->label);
		lv_obj_set_style_text_color(lbl, theme_colors()->text_primary, LV_PART_MAIN);
		lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

		if (item->value_str) {
			lv_obj_t *val = lv_label_create(row);
			lv_label_set_text(val, item->value_str);
			lv_obj_set_style_text_color(val,
						    theme_colors()->text_secondary,
						    LV_PART_MAIN);
			lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
		}

		s_rows[i] = row;
	}
	for (uint8_t i = row_count; i < SCREEN_MENU_MAX_ROWS; i++) {
		s_rows[i] = NULL;
	}

	highlight_row(s_sel, true);

	return scr;
}

void screen_menu_destroy(lv_obj_t *screen)
{
	lv_obj_delete(screen);
}

void screen_menu_update(lv_obj_t *screen)
{
	ARG_UNUSED(screen);
	/* Value strings are static for now; refresh when settings persistence added. */
}

void screen_menu_handle_action(lv_obj_t *screen, ui_action_t action)
{
	ARG_UNUSED(screen);

	uint8_t count = s_count > SCREEN_MENU_MAX_ROWS ? SCREEN_MENU_MAX_ROWS : s_count;

	if (count == 0) {
		return;
	}

	switch (action) {
	case UI_ACTION_UP:
		highlight_row(s_sel, false);
		s_sel = (int8_t)((s_sel + count - 1) % count);
		highlight_row(s_sel, true);
		lv_obj_scroll_to_view(s_rows[s_sel], LV_ANIM_OFF);
		break;
	case UI_ACTION_DOWN:
		highlight_row(s_sel, false);
		s_sel = (int8_t)((s_sel + 1) % count);
		highlight_row(s_sel, true);
		lv_obj_scroll_to_view(s_rows[s_sel], LV_ANIM_OFF);
		break;
	case UI_ACTION_OK:
		if (s_sel < count && s_items[s_sel].on_select) {
			s_items[s_sel].on_select();
		}
		break;
	default:
		break;
	}
}
