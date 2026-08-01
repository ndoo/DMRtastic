// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "screen_fm_channel.h"
#include "../status_bar.h"
#include "../theme.h"
#include "app.h"
#include "controller/channel_controller.h"
#include "model/codeplug.h"

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

/* Stored via lv_obj_set_user_data so widget pointers are scoped to screen lifetime. */
typedef struct {
	lv_obj_t *name_label;
	lv_obj_t *freq_label;
	lv_obj_t *bw_label;
	lv_obj_t *css_label;
	/*
	 * channel_controller's index as of the last paint; -1 forces the first
	 * update() to paint unconditionally (0 is itself a valid "no in-use
	 * channel" value from the controller).
	 */
	int shown_index;
	bool shown_entry_active; /* whether the last paint was a digit-entry buffer */
} fm_channel_data_t;

static void fmt_freq(char *buf, size_t len, uint32_t hz)
{
	uint32_t mhz = hz / 1000000U;
	uint32_t frac = (hz % 1000000U) / 10U; /* 5 digits: 100 kHz down to 10 Hz */

	snprintf(buf, len, "%3" PRIu32 ".%05" PRIu32 " MHz", mhz, frac);
}

/** Same CTCSS/DCS text convention as settings_controller.c's CTCSS/DCS row and the
 * console's console_format_css() -- "103.5Hz", "D023N"/"D023I", or "Off".
 */
static void fmt_css(char *buf, size_t len, struct cp_css css)
{
	if (css.type == CP_CSS_CTCSS) {
		snprintf(buf, len, "%u.%uHz", css.value / 10, css.value % 10);
	} else if (css.type == CP_CSS_DCS) {
		snprintf(buf, len, "D%03o%c", css.value, css.inverted ? 'I' : 'N');
	} else {
		snprintf(buf, len, "Off");
	}
}

/** Builds the FM Channel screen's widgets and populates the initial channel from the controller. */
lv_obj_t *screen_fm_channel_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	fm_channel_data_t *d = lv_malloc(sizeof(*d));
	__ASSERT_NO_MSG(d != NULL);
	d->shown_index = -1;
	d->shown_entry_active = false;
	lv_obj_set_user_data(scr, d);

	/* Channel index + name — top */
	d->name_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->name_label, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_label_set_text(d->name_label, "---");
	lv_obj_align(d->name_label, LV_ALIGN_TOP_MID, 0, 10);

	/* Frequency display — large, centre */
	d->freq_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->freq_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_set_style_text_font(d->freq_label, &lv_font_montserrat_20, LV_PART_MAIN);
	lv_label_set_text(d->freq_label, "----.----- MHz");
	lv_obj_align(d->freq_label, LV_ALIGN_CENTER, 0, 0);

	/* Bandwidth label — bottom-left */
	d->bw_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->bw_label, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_label_set_text(d->bw_label, "25K");
	lv_obj_align(d->bw_label, LV_ALIGN_BOTTOM_LEFT, 6, -6);

	/* RX CSS label — bottom-right */
	d->css_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->css_label, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_label_set_text(d->css_label, "Off");
	lv_obj_align(d->css_label, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

	screen_fm_channel_update(scr);

	return scr;
}

void screen_fm_channel_destroy(lv_obj_t *screen)
{
	fm_channel_data_t *d = lv_obj_get_user_data(screen);

	lv_free(d);
	lv_obj_delete(screen);
}

/** Repaints from channel_controller's cache; widgets only touched when the index (or the
 * digit-entry buffer) changes.
 */
void screen_fm_channel_update(lv_obj_t *screen)
{
	fm_channel_data_t *d = lv_obj_get_user_data(screen);

	/* Unconditional, like screen_fm_vfo_update()'s own trailing status_bar_set_mode()
	 * call -- must run even when index hasn't changed (e.g. just switched onto this
	 * screen from FM VFO via BACK), not just when the widgets below get repainted.
	 */
	status_bar_set_mode("CH");

	if (channel_controller_entry_active()) {
		char buf[24];

		/* Plain decimal, no leading-zero padding, unlike fm_vfo_controller's
		 * fixed-width '-'-padded frequency entry -- channel numbers have no
		 * fixed display width. Only the name_label swaps; freq/bw/css keep
		 * showing the still-current channel underneath.
		 */
		snprintf(buf, sizeof(buf), "Ch %d", channel_controller_entry_value());
		lv_label_set_text(d->name_label, buf);
		d->shown_entry_active = true;
		return;
	}

	int index = channel_controller_get_current_index();

	/* Force a repaint on the first inactive tick after an entry just ended (committed
	 * or cancelled), even if the index didn't change (e.g. cancelled, or committed to
	 * the channel already shown) -- same reasoning as screen_fm_vfo_update().
	 */
	if (d->shown_entry_active) {
		d->shown_index = -1;
	}
	d->shown_entry_active = false;

	if (index == d->shown_index) {
		return;
	}
	d->shown_index = index;

	struct cp_channel ch;
	char buf[24];

	if (!channel_controller_get_current(&ch)) {
		lv_label_set_text(d->name_label, "no channel");
		lv_label_set_text(d->freq_label, "----.----- MHz");
		lv_label_set_text(d->bw_label, "");
		lv_label_set_text(d->css_label, "");
		return;
	}

	snprintf(buf, sizeof(buf), "%03d %.16s", index, ch.name);
	lv_label_set_text(d->name_label, buf);

	fmt_freq(buf, sizeof(buf), ch.rxFreq);
	lv_label_set_text(d->freq_label, buf);

	lv_label_set_text(d->bw_label, (ch.chFlag4 & CP_CHANNEL_FLAG4_BW_25K) ? "25K" : "12.5K");

	fmt_css(buf, sizeof(buf), codeplug_decode_css(ch.rxTone));
	lv_label_set_text(d->css_label, buf);
}

/** Forwards a step to the controller; see channel_controller_step(). */
void screen_fm_channel_step(lv_obj_t *screen, bool up)
{
	(void)screen; /* controller state is a static singleton -- only one Channel screen exists */
	channel_controller_step(up);
}

/** Forwards a digit-entry keypress to the controller; see channel_controller_entry_digit(). */
void screen_fm_channel_entry_digit(lv_obj_t *screen, int digit)
{
	(void)screen;
	channel_controller_entry_digit(digit);
}

/** Forwards an entry-commit to the controller; see channel_controller_entry_commit(). */
void screen_fm_channel_entry_commit(lv_obj_t *screen)
{
	(void)screen;
	channel_controller_entry_commit();
}

/** Forwards an entry-cancel to the controller; see channel_controller_entry_cancel(). */
void screen_fm_channel_entry_cancel(lv_obj_t *screen)
{
	(void)screen;
	channel_controller_entry_cancel();
}
