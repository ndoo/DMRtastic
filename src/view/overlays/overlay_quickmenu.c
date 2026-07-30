// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "overlay_quickmenu.h"
#include "../theme.h"

#include <lvgl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define OVERLAY_W        140
#define ROW_H             18
#define OVERLAY_H       (ROW_H * ARRAY_SIZE(s_row_labels))
#define DISMISS_MS      4000

static const char *const s_row_labels[] = {
	"Bandwidth",
	"Squelch",
	"CTCSS/DCS",
};

static lv_obj_t   *s_panel;
static lv_obj_t   *s_rows[ARRAY_SIZE(s_row_labels)];
static lv_obj_t   *s_prev_focus; /* restored on hide */
static lv_timer_t *s_dismiss_timer;

static void dismiss_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);
	overlay_quickmenu_hide();
	lv_timer_pause(s_dismiss_timer);
}

/** Inverts a row's label color on focus so it reads against the saturated focus background. */
static void set_row_text_focused(lv_obj_t *row, bool focused)
{
	if (lv_obj_get_child_count(row) > 0) {
		lv_obj_set_style_text_color(lv_obj_get_child(row, 0),
					    focused ? theme_colors()->bg
						    : theme_colors()->text_primary,
					    LV_PART_MAIN);
	}
}

/** Row event callback: FOCUSED resets the auto-dismiss timer; CLICKED logs and closes the menu. */
static void row_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *row = lv_event_get_target(e);

	if (code == LV_EVENT_FOCUSED) {
		lv_timer_reset(s_dismiss_timer);
		set_row_text_focused(row, true);
		return;
	}
	if (code == LV_EVENT_DEFOCUSED) {
		set_row_text_focused(row, false);
		return;
	}

	const char *label = lv_event_get_user_data(e);

	LOG_INF("quick menu: %s not yet implemented", label);
	overlay_quickmenu_hide();
	lv_timer_pause(s_dismiss_timer);
}

/** Builds the hidden quick-menu panel and its rows, parented to lv_layer_top(). */
void overlay_quickmenu_create(void)
{
	lv_obj_t *layer = lv_layer_top();

	s_panel = lv_obj_create(layer);
	lv_obj_set_size(s_panel, OVERLAY_W, OVERLAY_H);
	lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(s_panel, theme_colors()->surface, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(s_panel, LV_OPA_90, LV_PART_MAIN);
	lv_obj_set_style_border_color(s_panel, theme_colors()->border, LV_PART_MAIN);
	lv_obj_set_style_border_width(s_panel, 1, LV_PART_MAIN);
	lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(s_panel, LV_SCROLLBAR_MODE_OFF);

	for (size_t i = 0; i < ARRAY_SIZE(s_row_labels); i++) {
		lv_obj_t *row = lv_obj_create(s_panel);
		lv_obj_set_size(row, lv_pct(100), ROW_H);
		lv_obj_set_pos(row, 0, (int32_t)(i * ROW_H));
		lv_obj_set_style_bg_color(row, theme_colors()->surface, LV_PART_MAIN);
		lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
		lv_obj_set_style_pad_hor(row, 4, LV_PART_MAIN);
		lv_obj_set_style_pad_ver(row, 0, LV_PART_MAIN);
		lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		/* Saturated focus background + inverted text, matching
		 * screen_settings.c's rows. */
		lv_obj_set_style_bg_color(row, theme_colors()->accent_secondary,
					  LV_PART_MAIN | LV_STATE_FOCUSED);
		lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)s_row_labels[i]);
		lv_obj_add_event_cb(row, row_cb, LV_EVENT_FOCUSED, (void *)s_row_labels[i]);
		lv_obj_add_event_cb(row, row_cb, LV_EVENT_DEFOCUSED, (void *)s_row_labels[i]);
		lv_group_add_obj(lv_group_get_default(), row);

		lv_obj_t *lbl = lv_label_create(row);
		lv_label_set_text(lbl, s_row_labels[i]);
		lv_obj_set_style_text_color(lbl, theme_colors()->text_primary, LV_PART_MAIN);
		lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

		s_rows[i] = row;
	}

	lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

	/* Timer starts paused; resumed on first show. */
	s_dismiss_timer = lv_timer_create(dismiss_cb, DISMISS_MS, NULL);
	lv_timer_pause(s_dismiss_timer);
}

/** Shows the panel, remembering the previously-focused object to restore on hide. */
void overlay_quickmenu_show(void)
{
	s_prev_focus = lv_group_get_focused(lv_group_get_default());

	lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
	lv_group_focus_obj(s_rows[0]);
	lv_timer_reset(s_dismiss_timer);
	lv_timer_resume(s_dismiss_timer);
}

void overlay_quickmenu_hide(void)
{
	lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
	if (s_prev_focus) {
		lv_group_focus_obj(s_prev_focus);
		s_prev_focus = NULL;
	}
}

bool overlay_quickmenu_is_active(void)
{
	return !lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}
