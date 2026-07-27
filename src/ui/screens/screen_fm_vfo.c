// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "screen_fm_vfo.h"
#include "../theme.h"
#include "../ui.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include <drivers/radio/radio_transceiver.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* Debounce before programming the AT1846S: coalesces a burst of Up/Down
 * clicks into one retune instead of blocking I2C per detent. */
#define VFO_RETUNE_DEBOUNCE_MS 100

/* Stored via lv_obj_set_user_data so widget pointers are scoped to screen lifetime. */
typedef struct {
	lv_obj_t *freq_label;
	lv_obj_t *rssi_bar;
	lv_obj_t *bw_label;
	lv_obj_t *sq_open_label;
	uint32_t  last_freq_hz;     /* value currently shown (== intended retune target) */
	uint8_t   last_rssi;
	bool      retune_pending;   /* last_freq_hz not yet programmed into hardware */
	int64_t   last_step_uptime; /* k_uptime_get() at the most recent Up/Down step */
} fm_vfo_data_t;

static void fmt_freq(char *buf, size_t len, uint32_t hz)
{
	uint32_t mhz  = hz / 1000000U;
	uint32_t frac = (hz % 1000000U) / 10U; /* 5 digits: 100 kHz down to 10 Hz */

	snprintf(buf, len, "%3" PRIu32 ".%05" PRIu32 " MHz", mhz, frac);
}

/** Updates both the shown frequency label and d->last_freq_hz. */
static void refresh_freq_label(fm_vfo_data_t *d, uint32_t hz)
{
	char buf[24];

	d->last_freq_hz = hz;
	fmt_freq(buf, sizeof(buf), hz);
	lv_label_set_text(d->freq_label, buf);
}

/** Builds the FM VFO screen's widgets and populates the initial frequency from the driver. */
lv_obj_t *screen_fm_vfo_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	fm_vfo_data_t *d = lv_malloc(sizeof(*d));
	__ASSERT_NO_MSG(d != NULL);
	d->last_freq_hz     = 0;
	d->last_rssi        = 0xFF;
	d->retune_pending   = false;
	d->last_step_uptime = 0;
	lv_obj_set_user_data(scr, d);

	/* Frequency display — large, top-centre */
	d->freq_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->freq_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_set_style_text_font(d->freq_label, &lv_font_montserrat_20, LV_PART_MAIN);
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
	lv_obj_set_style_text_color(d->sq_open_label,
				    theme_colors()->surface_alt, LV_PART_MAIN);
	lv_obj_align(d->sq_open_label, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

	/* Populate frequency from driver */
	screen_fm_vfo_update(scr);

	return scr;
}

void screen_fm_vfo_destroy(lv_obj_t *screen)
{
	fm_vfo_data_t *d = lv_obj_get_user_data(screen);
	lv_free(d);
	lv_obj_delete(screen);
}

/** Polls RSSI and drives the debounced retune state machine for a pending frequency change. */
void screen_fm_vfo_update(lv_obj_t *screen)
{
	fm_vfo_data_t *d = lv_obj_get_user_data(screen);

	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx->api;

	uint8_t signal = 0, noise = 0;
	api->get_rssi(trx, &signal, &noise);

	/* RSSI bar — update only on change */
	if (signal != d->last_rssi) {
		d->last_rssi = signal;
		lv_bar_set_value(d->rssi_bar, signal, LV_ANIM_OFF);
	}

	if (d->retune_pending) {
		/* Within the debounce window -- keep the optimistic shadow value on screen. */
		if (k_uptime_get() - d->last_step_uptime >= VFO_RETUNE_DEBOUNCE_MS) {
			uint32_t target = d->last_freq_hz;
			int rc = api->set_frequency(trx, target, false);

			if (rc < 0) {
				uint32_t hw_freq_hz;

				LOG_WRN("set_frequency(%u) failed: %d", target, rc);
				/* Roll the display back to whatever's actually tuned. */
				if (api->get_frequency &&
				    api->get_frequency(trx, &hw_freq_hz) == 0) {
					refresh_freq_label(d, hw_freq_hz);
				}
				d->retune_pending = false;
			} else if (d->last_freq_hz == target) {
				/* Shadow hasn't moved since target was captured -- now in sync. */
				d->retune_pending = false;
			}
			/* else: shadow moved again mid-write -- stay pending for the newer target. */
		}
	} else if (api->get_frequency) {
		/* Frequency label — update only on change */
		uint32_t freq_hz = 0;

		if (api->get_frequency(trx, &freq_hz) == 0 && freq_hz != d->last_freq_hz) {
			refresh_freq_label(d, freq_hz);
		}
	}

	/* Squelch state derived from noise byte, against whatever the menu set */
	bool sq_open = (noise < ui_get_squelch_threshold());

	lv_label_set_text(d->sq_open_label,
			  sq_open ? "[ OPEN ]" : "[ SQUELCH ]");
	lv_obj_set_style_text_color(d->sq_open_label,
				    sq_open ? theme_colors()->status_success
					    : theme_colors()->surface_alt,
				    LV_PART_MAIN);
}

/** Steps the shown frequency and arms the debounced retune; see screen_fm_vfo_update(). */
void screen_fm_vfo_step(lv_obj_t *screen, bool up)
{
	fm_vfo_data_t *d = lv_obj_get_user_data(screen);
	uint32_t step = ui_get_step_hz();
	uint32_t new_freq = up ? d->last_freq_hz + step
			       : (d->last_freq_hz > step ? d->last_freq_hz - step
							  : d->last_freq_hz);

	/* No I2C here -- just update the shown value and arm the debounced retune. */
	refresh_freq_label(d, new_freq);
	d->retune_pending   = true;
	d->last_step_uptime = k_uptime_get();
}
