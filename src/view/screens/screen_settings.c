// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "screen_settings.h"
#include "../theme.h"
#include "../fonts/fonts.h"
#include "app.h"
#include "model/radio_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include <lvgl.h>
#include <stdio.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define APP_VERSION_STR "0.1.0-dev"

#define ROW_PAD_X 4

#define SETTINGS_TAB_MAX_ROWS 20
#define SETTINGS_TAB_RADIO    0
#define SETTINGS_TAB_DISPLAY  1
#define SETTINGS_TAB_INFO     2

/* Two-level hierarchy: tab buttons and the active tab's rows/Info container never share
 * group membership (s_in_rows_level tracks which). Each level gets its own permanent
 * lv_group_t -- objects join exactly once at creation and are never removed -- and the
 * keypad indev is pointed at whichever group is current via app.c's
 * update_indev_group_attachment(), which queries screen_settings_get_active_group() every
 * tick. See enter_rows_level()/exit_rows_level().
 */
static lv_obj_t *s_tabview;
static bool s_in_rows_level;

static lv_group_t *s_tabbtn_group;
static lv_group_t *s_radio_group;
static lv_group_t *s_display_group;
static lv_group_t *s_info_group;
static lv_group_t *s_active_group; /* whichever of the row/info groups is current; only
				    * meaningful while s_in_rows_level
				    */

static lv_obj_t *s_radio_tab;
static lv_obj_t *s_display_tab;

static lv_obj_t *s_radio_rows[SETTINGS_TAB_MAX_ROWS];
static lv_obj_t *s_radio_val_labels[SETTINGS_TAB_MAX_ROWS]; /* NULL where value_str is NULL */
static const menu_item_t *s_radio_items;
static uint8_t s_radio_row_count;

static lv_obj_t *s_display_rows[SETTINGS_TAB_MAX_ROWS];
static lv_obj_t *s_display_val_labels[SETTINGS_TAB_MAX_ROWS];
static const menu_item_t *s_display_items;
static uint8_t s_display_row_count;

/* Info tab — its container is the sole member of s_info_group; labels refreshed by
 * screen_settings_update().
 */
static lv_obj_t *s_info_tab;
static lv_obj_t *s_info_uptime_label;
static lv_obj_t *s_info_rssi_label;

/** Inverts row [0]=label/[1]=value text color on focus so it reads against the focus background. */
static void set_row_text_focused(lv_obj_t *row, bool focused)
{
	uint32_t child_count = lv_obj_get_child_count(row);
	lv_color_t label_color = focused ? theme_colors()->bg : theme_colors()->text_primary;
	lv_color_t val_color = focused ? theme_colors()->bg : theme_colors()->text_secondary;

	if (child_count > 0) {
		lv_obj_set_style_text_color(lv_obj_get_child(row, 0), label_color, LV_PART_MAIN);
	}
	if (child_count > 1) {
		lv_obj_set_style_text_color(lv_obj_get_child(row, 1), val_color, LV_PART_MAIN);
	}
}

/** Row event callback: ENTER/click advances (dir=+1); FOCUSED scrolls the row into view and inverts
 * its text.
 */
static void row_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *row = lv_event_get_target(e);

	if (code == LV_EVENT_FOCUSED) {
		/* LV_ANIM_OFF: animated scroll dropped presses arriving
		 * mid-animation once there were 16 rows to scroll through.
		 */
		lv_obj_scroll_to_view(row, LV_ANIM_OFF);
		set_row_text_focused(row, true);
		return;
	}
	if (code == LV_EVENT_DEFOCUSED) {
		set_row_text_focused(row, false);
		return;
	}

	const menu_item_t *item = lv_event_get_user_data(e);

	if (item && item->on_select) {
		item->on_select(1);
	}
}

/** Builds one clickable settings row (label + optional value) under tab, joining group
 * permanently (never removed -- see the level-hierarchy comment near the top of this file).
 */
static lv_obj_t *add_row(lv_obj_t *tab, lv_group_t *group, const menu_item_t *item,
			 lv_obj_t **out_val_label)
{
	lv_obj_t *row = lv_obj_create(tab);

	/* keypad/encoder nav only, no touch targets to preserve -- row only needs to fit
	 * one line of UI_FONT_DEFAULT.
	 */
	lv_obj_set_size(row, lv_pct(100), ui_font_row_height(&UI_FONT_DEFAULT));
	lv_obj_set_style_bg_color(row, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_hor(row, ROW_PAD_X, LV_PART_MAIN);
	lv_obj_set_style_pad_ver(row, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
	lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
	/* Label flex-grows into whatever the value label (content-sized)
	 * doesn't need -- short values leave more room, per row.
	 */
	lv_obj_set_layout(row, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	/* Default theme's focus outline isn't visible enough here, so use an explicit
	 * saturated background instead (text inverts via set_row_text_focused()).
	 */
	lv_obj_set_style_bg_color(row, theme_colors()->accent_secondary,
				  LV_PART_MAIN | LV_STATE_FOCUSED);
	/* Cast away const: user_data is read-only in the callback. */
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)item);
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_FOCUSED, (void *)item);
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_DEFOCUSED, (void *)item);
	/* Lets screen_settings_handle_action() map the focused object back to its menu_item_t. */
	lv_obj_set_user_data(row, (void *)item);
	lv_group_add_obj(group, row);

	lv_obj_t *lbl = lv_label_create(row);
	lv_label_set_text(lbl, item->label);
	lv_obj_set_style_text_color(lbl, theme_colors()->text_primary, LV_PART_MAIN);
	/* LONG_DOT truncates with an ellipsis if flex-grow leaves too little
	 * room; needs a fixed height too, or it wraps instead of truncating.
	 */
	lv_obj_set_flex_grow(lbl, 1);
	/* Fixed height (needed so LONG_DOT truncates instead of wrapping) must match the value
	 * label's natural content height below, or the row's flex CENTER can't align them the
	 * same way: a box already at the row's full height can't be re-centered within it, so
	 * it renders text top-aligned while the value label's smaller auto-sized box does get
	 * centered. Plain line height (not ui_font_row_height()'s padded row height) is what the
	 * value label auto-sizes to, so use that here too.
	 */
	lv_obj_set_height(lbl, lv_font_get_line_height(&UI_FONT_DEFAULT));
	lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

	*out_val_label = NULL;
	if (item->value_str) {
		lv_obj_t *val = lv_label_create(row);
		lv_label_set_text(val, item->value_str);
		lv_obj_set_style_text_color(val, theme_colors()->text_secondary, LV_PART_MAIN);
		*out_val_label = val;
	}

	return row;
}

/** Builds a tab's row list from items (truncated to SETTINGS_TAB_MAX_ROWS); returns the row count
 * used.
 */
static uint8_t build_rows_tab(lv_obj_t *tab, lv_group_t *group, const menu_item_t *items,
			      uint8_t count, lv_obj_t **out_rows, lv_obj_t **out_val_labels)
{
	lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
	/* Zero the default flex gap -- wastes space with up to 16 rows. */
	lv_obj_set_style_pad_row(tab, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);
	lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

	if (count > SETTINGS_TAB_MAX_ROWS) {
		LOG_WRN("settings tab has %u rows, truncating to %u", count, SETTINGS_TAB_MAX_ROWS);
		count = SETTINGS_TAB_MAX_ROWS;
	}

	for (uint8_t i = 0; i < count; i++) {
		out_rows[i] = add_row(tab, group, &items[i], &out_val_labels[i]);
	}

	return count;
}

/** Re-reads each item's value_str buffer (mutated in place by on_select()) into its label. */
static void refresh_val_labels(const menu_item_t *items, lv_obj_t **val_labels, uint8_t count)
{
	for (uint8_t i = 0; i < count; i++) {
		if (val_labels[i]) {
			lv_label_set_text(val_labels[i], items[i].value_str);
		}
	}
}

/** Builds the Info tab's static version/kernel lines and the uptime/RSSI labels. */
static void build_info_tab(lv_obj_t *tab)
{
	lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(tab, 4, LV_PART_MAIN);
	lv_obj_set_style_pad_row(tab, 2, LV_PART_MAIN);

	static const char *const static_lines[] = {
		"DMRtastic " APP_VERSION_STR,
		"Zephyr " KERNEL_VERSION_STRING,
	};
	for (size_t i = 0; i < ARRAY_SIZE(static_lines); i++) {
		lv_obj_t *l = lv_label_create(tab);
		lv_label_set_text(l, static_lines[i]);
		lv_obj_set_style_text_color(l, theme_colors()->text_primary, LV_PART_MAIN);
	}

	s_info_uptime_label = lv_label_create(tab);
	lv_obj_set_style_text_color(s_info_uptime_label, theme_colors()->text_primary,
				    LV_PART_MAIN);

	s_info_rssi_label = lv_label_create(tab);
	lv_obj_set_style_text_color(s_info_rssi_label, theme_colors()->text_primary, LV_PART_MAIN);
}

/** Clears FOCUSED/FOCUS_KEY state on whichever object is currently focused within group,
 * without removing it from the group -- called when the keypad indev's attached group is
 * about to change (see screen_settings_get_active_group()/app.c's
 * update_indev_group_attachment()), so the old highlight doesn't linger. Safe with any group,
 * including one with no focused object yet.
 */
static void clear_focus_state(lv_group_t *group)
{
	lv_obj_t *focused = lv_group_get_focused(group);

	if (focused) {
		lv_obj_remove_state(focused, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
	}
}

/** Descends into idx's level: points the shared active-group pointer at that level's dedicated
 * group and focuses its first member. Rows/tab buttons/the Info container are permanent members
 * of their own group (added once in screen_settings_create()), so this never calls
 * lv_group_remove_obj() -- only clear_focus_state() clears the outgoing level's stale highlight.
 */
static void enter_rows_level(uint32_t idx)
{
	clear_focus_state(s_tabbtn_group);

	if (idx == SETTINGS_TAB_RADIO && s_radio_row_count > 0) {
		/* LV_ANIM_OFF: resets to the top before focusing row 0, rather than relying
		 * solely on row_cb()'s scroll_to_view() to correct a stale scroll offset left
		 * over from a previous visit.
		 */
		lv_obj_scroll_to_y(s_radio_tab, 0, LV_ANIM_OFF);
		s_active_group = s_radio_group;
		lv_group_focus_obj(s_radio_rows[0]);
	} else if (idx == SETTINGS_TAB_DISPLAY && s_display_row_count > 0) {
		lv_obj_scroll_to_y(s_display_tab, 0, LV_ANIM_OFF);
		s_active_group = s_display_group;
		lv_group_focus_obj(s_display_rows[0]);
	} else if (idx == SETTINGS_TAB_INFO) {
		s_active_group = s_info_group;
		lv_group_focus_obj(s_info_tab);
	} else {
		return;
	}
	s_in_rows_level = true;
}

static void tab_button_cb(lv_event_t *e)
{
	uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

	enter_rows_level(idx);
}

bool screen_settings_in_rows_level(void)
{
	return s_in_rows_level;
}

/** Reports which group should currently be attached to the keypad indev -- the active tab's
 * rows/Info container while descended, else the tab-button bar. Queried every tick by app.c's
 * update_indev_group_attachment().
 */
lv_group_t *screen_settings_get_active_group(void)
{
	return s_in_rows_level ? s_active_group : s_tabbtn_group;
}

/** Ascends to the tab level: clears the outgoing level's stale focus highlight and focuses the
 * active tab's button. No-op if already there.
 */
void screen_settings_exit_rows_level(void)
{
	if (!s_in_rows_level) {
		return;
	}

	lv_obj_t *focused = lv_group_get_focused(s_active_group);

	clear_focus_state(s_active_group);
	if (focused && (s_active_group == s_radio_group || s_active_group == s_display_group)) {
		set_row_text_focused(focused, false);
	}

	s_active_group = s_tabbtn_group;
	s_in_rows_level = false;

	uint32_t active = lv_tabview_get_tab_active(s_tabview);

	lv_group_focus_obj(lv_tabview_get_tab_button(s_tabview, (int32_t)active));
}

/** Builds the Radio/Display/Info tabview; starts at the tab level with tab buttons in the group. */
lv_obj_t *screen_settings_create(lv_obj_t *parent, const menu_item_t *radio_items,
				 uint8_t radio_count, const menu_item_t *display_items,
				 uint8_t display_count)
{
	lv_obj_t *tv = lv_tabview_create(parent);

	s_tabview = tv;

	lv_obj_set_size(tv, lv_pct(100), lv_pct(100));
	lv_tabview_set_tab_bar_size(tv, ui_font_row_height(&UI_FONT_DEFAULT));
	lv_obj_set_style_bg_color(tv, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(tv, 0, LV_PART_MAIN);

	/* One permanent group per level -- see the level-hierarchy comment near the top of this
	 * file for why membership is never churned after creation.
	 */
	s_tabbtn_group = lv_group_create();
	s_radio_group = lv_group_create();
	s_display_group = lv_group_create();
	s_info_group = lv_group_create();

	s_radio_tab = lv_tabview_add_tab(tv, "Radio");
	s_display_tab = lv_tabview_add_tab(tv, "Display");
	s_info_tab = lv_tabview_add_tab(tv, "Info");

	s_radio_items = radio_items;
	s_radio_row_count = build_rows_tab(s_radio_tab, s_radio_group, radio_items, radio_count,
					   s_radio_rows, s_radio_val_labels);
	s_display_items = display_items;
	s_display_row_count = build_rows_tab(s_display_tab, s_display_group, display_items,
					     display_count, s_display_rows, s_display_val_labels);
	build_info_tab(s_info_tab);
	lv_group_add_obj(s_info_group, s_info_tab);

	/* lv_group_add_obj() onto a previously-empty group implicitly focuses the first member
	 * added, without firing LV_EVENT_FOCUSED -- leaves a stray focus-background highlight
	 * (but not inverted text, since that's only set from the event) on each group's first
	 * row/the Info tab before the user has ever descended into anything. Strip that state
	 * immediately; enter_rows_level() re-focuses properly (with the event) on actual descent.
	 */
	clear_focus_state(s_radio_group);
	clear_focus_state(s_display_group);
	clear_focus_state(s_info_group);

	/* Tab level is the initial state: tab buttons' group is what starts attached to the
	 * keypad indev (see screen_settings_get_active_group()). Each button also gets a click
	 * handler so descending works even when Green hits the already-active tab.
	 */
	uint32_t tab_count = lv_tabview_get_tab_count(tv);

	for (uint32_t i = 0; i < tab_count; i++) {
		lv_obj_t *btn = lv_tabview_get_tab_button(tv, (int32_t)i);

		lv_group_add_obj(s_tabbtn_group, btn);
		lv_obj_add_event_cb(btn, tab_button_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
	}
	s_active_group = s_tabbtn_group;
	lv_group_focus_obj(lv_tabview_get_tab_button(tv, 0));

	screen_settings_update(tv);

	return tv;
}

/** Refreshes row value labels and the Info tab's uptime/RSSI text. */
void screen_settings_update(lv_obj_t *screen)
{
	ARG_UNUSED(screen);

	refresh_val_labels(s_radio_items, s_radio_val_labels, s_radio_row_count);
	refresh_val_labels(s_display_items, s_display_val_labels, s_display_row_count);

	char buf[32];

	snprintf(buf, sizeof(buf), "Uptime: %lld s", k_uptime_get() / 1000LL);
	lv_label_set_text(s_info_uptime_label, buf);

	uint8_t signal, noise;

	radio_state_get_rssi(&signal, &noise);
	snprintf(buf, sizeof(buf), "RSSI sig=%u noise=%u", signal, noise);
	lv_label_set_text(s_info_rssi_label, buf);
}

/** Handles ENCODER_CW/CCW by calling the focused row's on_select(±1); no-op at the tab level. */
void screen_settings_handle_action(lv_obj_t *screen, ui_action_t action)
{
	ARG_UNUSED(screen);

	int8_t dir;

	switch (action) {
	case UI_ACTION_ENCODER_CW:
		dir = 1;
		break;
	case UI_ACTION_ENCODER_CCW:
		dir = -1;
		break;
	default:
		return;
	}

	lv_obj_t *focused = s_in_rows_level ? lv_group_get_focused(s_active_group) : NULL;
	const menu_item_t *item = focused ? lv_obj_get_user_data(focused) : NULL;

	if (item && item->on_select) {
		item->on_select(dir);
	}
}
