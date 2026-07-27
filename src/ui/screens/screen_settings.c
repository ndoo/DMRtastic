// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "screen_settings.h"
#include "../theme.h"
#include "../ui.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include <lvgl.h>
#include <stdio.h>

#include <drivers/radio/radio_transceiver.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define APP_VERSION_STR "0.1.0-dev"

#define ROW_H     16
#define ROW_PAD_X  4
#define TAB_BAR_H  18

#define SETTINGS_TAB_MAX_ROWS 8
#define SETTINGS_TAB_RADIO    0
#define SETTINGS_TAB_DISPLAY  1

/*
 * Two-level hierarchy (OpenGD77 convention, applies to every tabview):
 * Up/Down cycle through tabs only until Green/Enter "descends" into the
 * active tab's row list -- only then do Up/Down cycle through rows, and
 * only Red/Back "ascends" back out to the tab level. The two levels never
 * share group membership: tab buttons are group members only at the tab
 * level, the active tab's rows only at the row level. s_in_rows_level
 * tracks which; enter_rows_level()/screen_settings_exit_rows_level() do the
 * actual group-membership swap.
 *
 * This replaces the earlier single flat group (tab buttons + active tab's
 * rows all navigable together) -- lv_tabview doesn't hide inactive tabs'
 * content (lv_tabview_set_active() only scrolls the tab container), so
 * without managing membership explicitly, Up/Down's PREV/NEXT traversal
 * would wander into off-screen rows from whichever tab isn't showing.
 */
static lv_obj_t *s_tabview;
static bool       s_in_rows_level;

static lv_obj_t *s_radio_rows[SETTINGS_TAB_MAX_ROWS];
static lv_obj_t *s_radio_val_labels[SETTINGS_TAB_MAX_ROWS]; /* NULL where value_str is NULL */
static const menu_item_t *s_radio_items;
static uint8_t    s_radio_row_count;

static lv_obj_t *s_display_rows[SETTINGS_TAB_MAX_ROWS];
static lv_obj_t *s_display_val_labels[SETTINGS_TAB_MAX_ROWS];
static const menu_item_t *s_display_items;
static uint8_t    s_display_row_count;

/* Info tab labels — refreshed by screen_settings_update(). */
static lv_obj_t *s_info_uptime_label;
static lv_obj_t *s_info_rssi_label;

/*
 * Row children are always [0]=label, optionally [1]=value label (see
 * add_row()) -- flip both to the theme's background color on focus so
 * they read against the saturated focus-background below, and restore
 * their normal colors on defocus. LVGL's text_color is an inheritable
 * style property in principle, but inheritance resolves from the PARENT's
 * *own* computed value for that property, not its state, and a plain
 * container never has its own text_color -- so a child label doesn't pick
 * up the row's FOCUSED-state styling automatically; setting it directly on
 * focus/defocus is the reliable way to get inverted text.
 */
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

/* Event callback for row buttons. ENTER/click always advances (dir=+1),
 * matching the old cycle-on-click behavior; the encoder can go either way
 * (see screen_settings_handle_action()). LVGL's group focus doesn't
 * auto-scroll a newly-focused object into view (the old hand-rolled cursor
 * did this explicitly via lv_obj_scroll_to_view() on every Up/Down) -- catch
 * LV_EVENT_FOCUSED here to restore that when a row scrolls partly/fully off
 * the tab's viewport. */
static void row_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *row = lv_event_get_target(e);

	if (code == LV_EVENT_FOCUSED) {
		lv_obj_scroll_to_view(row, LV_ANIM_ON);
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

static lv_obj_t *add_row(lv_obj_t *tab, const menu_item_t *item, lv_obj_t **out_val_label)
{
	lv_obj_t *row = lv_obj_create(tab);

	lv_obj_set_size(row, lv_pct(100), ROW_H);
	lv_obj_set_style_bg_color(row, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_hor(row, ROW_PAD_X, LV_PART_MAIN);
	lv_obj_set_style_pad_ver(row, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
	lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
	/* Explicit focus feedback -- the default theme's focus outline isn't
	 * visible enough on this display's low contrast. accent_secondary is
	 * the brighter/more saturated of the two accent colors (see
	 * theme.c); text color inverts to the background color via
	 * set_row_text_focused() so it stays legible against it. */
	lv_obj_set_style_bg_color(row, theme_colors()->accent_secondary,
				  LV_PART_MAIN | LV_STATE_FOCUSED);
	/* Cast away const: user_data is read-only in the callback. */
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)item);
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_FOCUSED, (void *)item);
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_DEFOCUSED, (void *)item);
	/* Lets screen_settings_handle_action() map the currently-focused
	 * object (from lv_group_get_focused()) back to its menu_item_t when
	 * the encoder turns. */
	lv_obj_set_user_data(row, (void *)item);
	/* Deliberately NOT added to the group here -- see the comment above
	 * s_radio_rows. Membership is managed by set_tab_rows_active(). */

	lv_obj_t *lbl = lv_label_create(row);
	lv_label_set_text(lbl, item->label);
	lv_obj_set_style_text_color(lbl, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

	*out_val_label = NULL;
	if (item->value_str) {
		lv_obj_t *val = lv_label_create(row);
		lv_label_set_text(val, item->value_str);
		lv_obj_set_style_text_color(val, theme_colors()->text_secondary, LV_PART_MAIN);
		lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
		*out_val_label = val;
	}

	return row;
}

static uint8_t build_rows_tab(lv_obj_t *tab, const menu_item_t *items, uint8_t count,
			       lv_obj_t **out_rows, lv_obj_t **out_val_labels)
{
	lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);
	lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

	if (count > SETTINGS_TAB_MAX_ROWS) {
		LOG_WRN("settings tab has %u rows, truncating to %u", count,
			SETTINGS_TAB_MAX_ROWS);
		count = SETTINGS_TAB_MAX_ROWS;
	}

	for (uint8_t i = 0; i < count; i++) {
		out_rows[i] = add_row(tab, &items[i], &out_val_labels[i]);
	}

	return count;
}

/* Value strings (e.g. s_squelch_val_buf in ui.c) are mutated in place by
 * each row's on_select() -- refresh the on-screen labels from them here
 * rather than relying on a per-item change callback. */
static void refresh_val_labels(const menu_item_t *items, lv_obj_t **val_labels, uint8_t count)
{
	for (uint8_t i = 0; i < count; i++) {
		if (val_labels[i]) {
			lv_label_set_text(val_labels[i], items[i].value_str);
		}
	}
}

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
	lv_obj_set_style_text_color(s_info_uptime_label, theme_colors()->text_primary, LV_PART_MAIN);

	s_info_rssi_label = lv_label_create(tab);
	lv_obj_set_style_text_color(s_info_rssi_label, theme_colors()->text_primary, LV_PART_MAIN);
}

/*
 * lv_group_remove_obj() detaches an object from the group's traversal list,
 * but doesn't reliably clear the FOCUSED/FOCUS_KEY state bits it leaves
 * carrying (state belongs to the object, not the group -- removal doesn't
 * imply "unfocus" the way moving focus to a different object does). Left
 * alone, a tab button removed from the group while it happened to still be
 * the last-focused one keeps rendering its focus-highlight style forever,
 * even after Green descends into a *different* tab's rows. Clear both bits
 * explicitly on every removal so switching levels never leaves a stale
 * highlight behind.
 */
static void set_rows_in_group(lv_obj_t **rows, uint8_t count, bool add)
{
	for (uint8_t i = 0; i < count; i++) {
		if (add) {
			lv_group_add_obj(lv_group_get_default(), rows[i]);
		} else {
			lv_group_remove_obj(rows[i]);
			lv_obj_remove_state(rows[i], LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
		}
	}
}

static void set_tab_buttons_in_group(bool add)
{
	uint32_t tab_count = lv_tabview_get_tab_count(s_tabview);

	for (uint32_t i = 0; i < tab_count; i++) {
		lv_obj_t *btn = lv_tabview_get_tab_button(s_tabview, (int32_t)i);

		if (add) {
			lv_group_add_obj(lv_group_get_default(), btn);
		} else {
			lv_group_remove_obj(btn);
			lv_obj_remove_state(btn, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
		}
	}
}

/* Descends into idx's row list: swaps group membership from tab buttons to
 * that tab's rows and focuses the first one. Called directly from each tab
 * button's own LV_EVENT_CLICKED handler (not lv_tabview's VALUE_CHANGED
 * event) so it fires even when Green is pressed on the tab that's already
 * active -- lv_tabview only fires VALUE_CHANGED when the active tab index
 * actually changes. */
static void enter_rows_level(uint32_t idx)
{
	set_tab_buttons_in_group(false);
	set_rows_in_group(s_radio_rows, s_radio_row_count, idx == SETTINGS_TAB_RADIO);
	set_rows_in_group(s_display_rows, s_display_row_count, idx == SETTINGS_TAB_DISPLAY);
	s_in_rows_level = true;

	if (idx == SETTINGS_TAB_RADIO && s_radio_row_count > 0) {
		lv_group_focus_obj(s_radio_rows[0]);
	} else if (idx == SETTINGS_TAB_DISPLAY && s_display_row_count > 0) {
		lv_group_focus_obj(s_display_rows[0]);
	}
	/* Info tab has no rows -- nothing to focus, but Back still ascends
	 * fine since it doesn't depend on a focused object existing. */
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

void screen_settings_exit_rows_level(void)
{
	if (!s_in_rows_level) {
		return;
	}

	uint32_t active = lv_tabview_get_tab_active(s_tabview);

	set_rows_in_group(s_radio_rows, s_radio_row_count, false);
	set_rows_in_group(s_display_rows, s_display_row_count, false);
	set_tab_buttons_in_group(true);
	s_in_rows_level = false;

	lv_group_focus_obj(lv_tabview_get_tab_button(s_tabview, (int32_t)active));
}

lv_obj_t *screen_settings_create(lv_obj_t *parent,
				  const menu_item_t *radio_items, uint8_t radio_count,
				  const menu_item_t *display_items, uint8_t display_count)
{
	lv_obj_t *tv = lv_tabview_create(parent);

	s_tabview = tv;

	lv_obj_set_size(tv, lv_pct(100), lv_pct(100));
	lv_tabview_set_tab_bar_size(tv, TAB_BAR_H);
	lv_obj_set_style_bg_color(tv, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(tv, 0, LV_PART_MAIN);

	lv_obj_t *radio_tab   = lv_tabview_add_tab(tv, "Radio");
	lv_obj_t *display_tab = lv_tabview_add_tab(tv, "Display");
	lv_obj_t *info_tab    = lv_tabview_add_tab(tv, "Info");

	s_radio_items = radio_items;
	s_radio_row_count = build_rows_tab(radio_tab, radio_items, radio_count,
					    s_radio_rows, s_radio_val_labels);
	s_display_items = display_items;
	s_display_row_count = build_rows_tab(display_tab, display_items, display_count,
					      s_display_rows, s_display_val_labels);
	build_info_tab(info_tab);

	/* Tab level is the initial state: tab buttons join the group (rows
	 * don't, until Green descends into one -- see enter_rows_level()).
	 * Each button gets its own index-carrying click handler in addition
	 * to lv_tabview's internal one (which only switches the visible
	 * content) so descending works even when Green is pressed on the
	 * tab that's already active. */
	uint32_t tab_count = lv_tabview_get_tab_count(tv);

	for (uint32_t i = 0; i < tab_count; i++) {
		lv_obj_t *btn = lv_tabview_get_tab_button(tv, (int32_t)i);

		lv_group_add_obj(lv_group_get_default(), btn);
		lv_obj_add_event_cb(btn, tab_button_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
	}
	lv_group_focus_obj(lv_tabview_get_tab_button(tv, 0));

	screen_settings_update(tv);

	return tv;
}

void screen_settings_update(lv_obj_t *screen)
{
	ARG_UNUSED(screen);

	refresh_val_labels(s_radio_items, s_radio_val_labels, s_radio_row_count);
	refresh_val_labels(s_display_items, s_display_val_labels, s_display_row_count);

	char buf[32];

	snprintf(buf, sizeof(buf), "Uptime: %lld s", k_uptime_get() / 1000LL);
	lv_label_set_text(s_info_uptime_label, buf);

	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx->api;
	uint8_t signal = 0, noise = 0;

	api->get_rssi(trx, &signal, &noise);
	snprintf(buf, sizeof(buf), "RSSI sig=%u noise=%u", signal, noise);
	lv_label_set_text(s_info_rssi_label, buf);
}

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

	lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
	const menu_item_t *item = focused ? lv_obj_get_user_data(focused) : NULL;

	if (item && item->on_select) {
		item->on_select(dir);
	}
}
