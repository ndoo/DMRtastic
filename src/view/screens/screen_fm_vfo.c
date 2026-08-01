// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "screen_fm_vfo.h"
#include "../status_bar.h"
#include "../theme.h"
#include "../fonts/fonts.h"
#include "app.h"
#include "controller/fm_vfo_controller.h"
#include "controller/scan_controller.h"
#include "model/radio_settings.h"

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

/* Stored via lv_obj_set_user_data so widget pointers are scoped to screen lifetime. */
typedef struct {
	lv_obj_t *freq_label;
	lv_obj_t *rssi_bar;
	lv_obj_t *bw_label;
	lv_obj_t *sq_open_label;
	uint32_t shown_freq_hz; /* value currently painted, for change-detection only */
	uint8_t shown_rssi;
	bool shown_entry_active; /* whether the last paint was a digit-entry buffer */
} fm_vfo_data_t;

static void fmt_freq(char *buf, size_t len, uint32_t hz)
{
	uint32_t mhz = hz / 1000000U;
	uint32_t frac = (hz % 1000000U) / 10U; /* 5 digits: 100 kHz down to 10 Hz */

	snprintf(buf, len, "%3" PRIu32 ".%05" PRIu32 " MHz", mhz, frac);
}

/** Renders the in-progress digit-entry buffer, unfilled positions shown as '-' -- same
 * 3-digit-MHz + 5-digit-sub-MHz split as fmt_freq().
 */
static void fmt_entry(char *buf, size_t len, int digit_count)
{
	char mhz[4], frac[6];

	for (int i = 0; i < 3; i++) {
		mhz[i] = (i < digit_count) ? (char)('0' + fm_vfo_controller_entry_get_digit(i))
					   : '-';
	}
	mhz[3] = '\0';

	for (int i = 0; i < 5; i++) {
		int pos = 3 + i;

		frac[i] = (pos < digit_count) ? (char)('0' + fm_vfo_controller_entry_get_digit(pos))
					      : '-';
	}
	frac[5] = '\0';

	snprintf(buf, len, "%s.%s MHz", mhz, frac);
}

/** Builds the FM VFO screen's widgets and populates the initial frequency from the controller. */
lv_obj_t *screen_fm_vfo_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	fm_vfo_data_t *d = lv_malloc(sizeof(*d));
	__ASSERT_NO_MSG(d != NULL);
	d->shown_freq_hz = 0;
	d->shown_rssi = 0xFF;
	d->shown_entry_active = false;
	lv_obj_set_user_data(scr, d);

	/* Frequency display — large, top-centre */
	d->freq_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->freq_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_set_style_text_font(d->freq_label, &UI_FONT_LARGE, LV_PART_MAIN);
	lv_label_set_text(d->freq_label, "----.----- MHz");
	lv_obj_align(d->freq_label, LV_ALIGN_TOP_MID, 0, 10);

	/* RSSI bar — middle */
	d->rssi_bar = lv_bar_create(scr);
	lv_obj_set_size(d->rssi_bar, lv_pct(90), 8);
	lv_obj_align(d->rssi_bar, LV_ALIGN_CENTER, 0, 10);
	lv_bar_set_range(d->rssi_bar, 0, 255);
	lv_bar_set_value(d->rssi_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(d->rssi_bar, theme_colors()->surface_alt, LV_PART_MAIN);
	lv_obj_set_style_bg_color(d->rssi_bar, theme_colors()->text_primary, LV_PART_INDICATOR);

	/* Bandwidth label — bottom-left */
	d->bw_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->bw_label, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_label_set_text(d->bw_label, "25K");
	lv_obj_align(d->bw_label, LV_ALIGN_BOTTOM_LEFT, 6, -6);

	/* Squelch open/closed label — bottom-right */
	d->sq_open_label = lv_label_create(scr);
	lv_label_set_text(d->sq_open_label, "[ SQUELCH ]");
	lv_obj_set_style_text_color(d->sq_open_label, theme_colors()->surface_alt, LV_PART_MAIN);
	lv_obj_align(d->sq_open_label, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

	/* Populate frequency from the controller */
	screen_fm_vfo_update(scr);

	return scr;
}

void screen_fm_vfo_destroy(lv_obj_t *screen)
{
	fm_vfo_data_t *d = lv_obj_get_user_data(screen);
	lv_free(d);
	lv_obj_delete(screen);
}

/** Drives the controller tick, then paints whatever it reports; widgets only touched on change. */
void screen_fm_vfo_update(lv_obj_t *screen)
{
	fm_vfo_data_t *d = lv_obj_get_user_data(screen);

	/* scan_controller only progresses while this screen's update() is running, same
	 * reasoning as screen_fm_channel_update()'s own scan_controller_tick() call -- a
	 * no-op unless VFO scan (Milestone 5b) is the currently active mode. May step
	 * fm_vfo_controller to a new frequency, which the frequency check below picks up
	 * same as a manual step.
	 */
	scan_controller_tick_vfo();

	fm_vfo_controller_tick();

	uint8_t rssi = fm_vfo_controller_get_rssi();

	if (rssi != d->shown_rssi) {
		d->shown_rssi = rssi;
		lv_bar_set_value(d->rssi_bar, rssi, LV_ANIM_OFF);
	}

	bool entry_active = fm_vfo_controller_entry_active();

	if (entry_active) {
		char buf[24];

		fmt_entry(buf, sizeof(buf), fm_vfo_controller_entry_digit_count());
		lv_label_set_text(d->freq_label, buf);
		d->shown_entry_active = true;
	} else {
		uint32_t freq_hz = fm_vfo_controller_get_frequency_hz();

		/* Force a repaint on the first inactive tick after an entry just
		 * ended (committed or cancelled), even if the frequency itself
		 * didn't change (e.g. a cancelled entry).
		 */
		if (d->shown_entry_active) {
			d->shown_freq_hz = ~freq_hz;
		}
		d->shown_entry_active = false;

		if (freq_hz != d->shown_freq_hz) {
			char buf[24];

			d->shown_freq_hz = freq_hz;
			fmt_freq(buf, sizeof(buf), freq_hz);
			lv_label_set_text(d->freq_label, buf);
		}
	}

	bool sq_open = fm_vfo_controller_get_squelch_open();

	lv_label_set_text(d->sq_open_label, sq_open ? "[ OPEN ]" : "[ SQUELCH ]");
	lv_obj_set_style_text_color(d->sq_open_label,
				    sq_open ? theme_colors()->status_success
					    : theme_colors()->surface_alt,
				    LV_PART_MAIN);

	lv_label_set_text(d->bw_label, settings_get_bandwidth_is_25k() ? "25K" : "12.5K");

	status_bar_set_mode(fm_vfo_controller_get_current_vfo() == 0 ? "FM A" : "FM B");
}

/** Forwards a step to the controller; see fm_vfo_controller_step(). */
void screen_fm_vfo_step(lv_obj_t *screen, bool up)
{
	(void)screen; /* controller state is a static singleton -- only one VFO screen exists */
	fm_vfo_controller_step(up);
}

/** Forwards a digit-entry keypress to the controller; see fm_vfo_controller_entry_digit(). */
void screen_fm_vfo_entry_digit(lv_obj_t *screen, int digit)
{
	(void)screen;
	fm_vfo_controller_entry_digit(digit);
}

/** Forwards a backspace to the controller; see fm_vfo_controller_entry_backspace(). */
void screen_fm_vfo_entry_backspace(lv_obj_t *screen)
{
	(void)screen;
	fm_vfo_controller_entry_backspace();
}

/** Forwards an entry-cancel to the controller; see fm_vfo_controller_entry_cancel(). */
void screen_fm_vfo_entry_cancel(lv_obj_t *screen)
{
	(void)screen;
	fm_vfo_controller_entry_cancel();
}
