// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "status_bar.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "app.h"
#include "model/battery.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* RSSI bar geometry: four bars of increasing height, left-aligned. */
#define RSSI_BARS    4
#define RSSI_BAR_W   3
#define RSSI_BAR_GAP 2

static lv_obj_t *s_mode_label;
static lv_obj_t *s_scan_label;
static lv_obj_t *s_tx_label;
static lv_obj_t *s_rssi_bars[RSSI_BARS];
static lv_obj_t *s_gps_dot;
static lv_obj_t *s_bat_label;

static uint8_t s_last_bars = 0xFF; /* force first draw */

int32_t status_bar_height(void)
{
	return ui_font_row_height(&UI_FONT_DEFAULT);
}

/** Builds the status bar's mode/scan/TX labels, RSSI bars, GPS dot, and battery label. */
void status_bar_create(lv_obj_t *parent)
{
	/* Mode label — left zone */
	s_mode_label = lv_label_create(parent);
	lv_label_set_text(s_mode_label, "FM");
	lv_obj_set_style_text_color(s_mode_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_align(s_mode_label, LV_ALIGN_LEFT_MID, 2, 0);

	/* Scan indicator — centre, hidden by default */
	s_scan_label = lv_label_create(parent);
	lv_label_set_text(s_scan_label, "SCAN");
	lv_obj_set_style_text_color(s_scan_label, theme_colors()->status_warning, LV_PART_MAIN);
	lv_obj_align(s_scan_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(s_scan_label, LV_OBJ_FLAG_HIDDEN);

	/* TX indicator — positioned relative to the mode label's actual rendered width (see
	 * status_bar_set_mode()), not a fixed offset, since mode text ranges from "FM" to
	 * "CH 123" -- hidden by default.
	 */
	s_tx_label = lv_label_create(parent);
	lv_label_set_text(s_tx_label, "TX");
	lv_obj_set_style_text_color(s_tx_label, theme_colors()->status_error, LV_PART_MAIN);
	lv_obj_align_to(s_tx_label, s_mode_label, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
	lv_obj_add_flag(s_tx_label, LV_OBJ_FLAG_HIDDEN);

	/* RSSI bars — right zone, bottom-aligned, ascending heights */
	static const uint8_t bar_heights[RSSI_BARS] = {3, 5, 7, 9};
	int32_t right_x = -68; /* offset from right edge */

	for (int i = 0; i < RSSI_BARS; i++) {
		s_rssi_bars[i] = lv_obj_create(parent);
		lv_obj_set_size(s_rssi_bars[i], RSSI_BAR_W, bar_heights[i]);
		lv_obj_set_style_radius(s_rssi_bars[i], 0, LV_PART_MAIN);
		lv_obj_set_style_border_width(s_rssi_bars[i], 0, LV_PART_MAIN);
		lv_obj_align(s_rssi_bars[i], LV_ALIGN_RIGHT_MID,
			     right_x + i * (RSSI_BAR_W + RSSI_BAR_GAP), 0);
	}

	/* GPS dot — 4×4 px square, grey placeholder */
	s_gps_dot = lv_obj_create(parent);
	lv_obj_set_size(s_gps_dot, 4, 4);
	lv_obj_set_style_radius(s_gps_dot, 2, LV_PART_MAIN);
	lv_obj_set_style_border_width(s_gps_dot, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(s_gps_dot, theme_colors()->surface_alt, LV_PART_MAIN);
	lv_obj_align(s_gps_dot, LV_ALIGN_RIGHT_MID, -48, 0);

	/* Battery label — rightmost */
	s_bat_label = lv_label_create(parent);
	lv_label_set_text(s_bat_label, "--");
	lv_obj_set_style_text_color(s_bat_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_align(s_bat_label, LV_ALIGN_RIGHT_MID, -2, 0);

	/* Initial RSSI draw */
	status_bar_update();
}

/** Formats s_bat_label from battery.c's getters and the %/V unit pref; redraws only on change. */
static void status_bar_update_battery(void)
{
	static char buf[8];
	static uint16_t s_last_mv = 0xFFFF; /* sentinel: force first draw */
	static bool s_last_is_pct = true;
	static bool s_first = true;

	bool is_pct = settings_get_battery_unit_is_percent();
	uint16_t mv = battery_get_millivolts();

	if (!s_first && mv == s_last_mv && is_pct == s_last_is_pct) {
		return;
	}
	s_first = false;
	s_last_mv = mv;
	s_last_is_pct = is_pct;

	if (is_pct) {
		snprintf(buf, sizeof(buf), "%u%%", battery_get_percent());
	} else {
		snprintf(buf, sizeof(buf), "%u.%01uV", mv / 1000U, (mv % 1000U) / 100U);
	}
	lv_label_set_text(s_bat_label, buf);
}

/** Polls RSSI and battery, redrawing each only if its rendered value changed. */
void status_bar_update(void)
{
	uint8_t bars = radio_state_get_rssi_bars();

	if (bars != s_last_bars) {
		s_last_bars = bars;

		for (int i = 0; i < RSSI_BARS; i++) {
			lv_color_t c = (i < bars) ? theme_colors()->text_primary
						  : theme_colors()->surface_alt;
			lv_obj_set_style_bg_color(s_rssi_bars[i], c, LV_PART_MAIN);
		}
	}

	status_bar_update_battery();
}

void status_bar_set_mode(const char *mode)
{
	lv_label_set_text(s_mode_label, mode);
	/* Re-tracks the mode label's width, which changes with its text (e.g. "FM" vs.
	 * "CH 123"), so the TX indicator doesn't overlap it.
	 */
	lv_obj_align_to(s_tx_label, s_mode_label, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
}

void status_bar_set_tx(bool active)
{
	if (active) {
		lv_obj_clear_flag(s_tx_label, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(s_tx_label, LV_OBJ_FLAG_HIDDEN);
	}
}
