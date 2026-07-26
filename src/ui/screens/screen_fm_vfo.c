// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "screen_fm_vfo.h"
#include "../ui.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include <drivers/radio/radio_transceiver.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/*
 * Widget pointers stored in a struct parented to the screen object via
 * lv_obj_set_user_data so they're naturally scoped to screen lifetime.
 */
typedef struct {
	lv_obj_t *freq_label;
	lv_obj_t *rssi_bar;
	lv_obj_t *bw_label;
	lv_obj_t *sq_open_label;
	uint32_t  last_freq_hz;
	uint8_t   last_rssi;
} fm_vfo_data_t;

static void fmt_freq(char *buf, size_t len, uint32_t hz)
{
	uint32_t mhz     = hz / 1000000U;
	uint32_t khz     = (hz % 1000000U) / 1000U;
	uint32_t sub_khz = (hz % 1000U) / 100U;

	snprintf(buf, len, "%3" PRIu32 ".%03" PRIu32 " %1" PRIu32 "  MHz",
		 mhz, khz, sub_khz);
}

lv_obj_t *screen_fm_vfo_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	fm_vfo_data_t *d = lv_malloc(sizeof(*d));
	__ASSERT_NO_MSG(d != NULL);
	d->last_freq_hz = 0;
	d->last_rssi    = 0xFF;
	lv_obj_set_user_data(scr, d);

	/* Frequency display — large, top-centre */
	d->freq_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->freq_label, lv_color_white(), LV_PART_MAIN);
	lv_obj_set_style_text_font(d->freq_label, &lv_font_montserrat_20, LV_PART_MAIN);
	lv_label_set_text(d->freq_label, "--- --- ---  MHz");
	lv_obj_align(d->freq_label, LV_ALIGN_TOP_MID, 0, 10);

	/* RSSI bar — middle */
	d->rssi_bar = lv_bar_create(scr);
	lv_obj_set_size(d->rssi_bar, lv_pct(90), 8);
	lv_obj_align(d->rssi_bar, LV_ALIGN_CENTER, 0, 10);
	lv_bar_set_range(d->rssi_bar, 0, 255);
	lv_bar_set_value(d->rssi_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(d->rssi_bar, lv_color_hex(0x333333), LV_PART_MAIN);
	lv_obj_set_style_bg_color(d->rssi_bar, lv_color_white(), LV_PART_INDICATOR);

	/* Bandwidth label — bottom-left */
	d->bw_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->bw_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
	lv_label_set_text(d->bw_label, "25K");
	lv_obj_align(d->bw_label, LV_ALIGN_BOTTOM_LEFT, 6, -6);

	/* Squelch open/closed label — bottom-right */
	d->sq_open_label = lv_label_create(scr);
	lv_label_set_text(d->sq_open_label, "[ SQUELCH ]");
	lv_obj_set_style_text_color(d->sq_open_label,
				    lv_color_hex(0x505050), LV_PART_MAIN);
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

	/* Frequency label — update only on change */
	uint32_t freq_hz = 0;

	if (api->get_frequency && api->get_frequency(trx, &freq_hz) == 0 &&
	    freq_hz != d->last_freq_hz) {
		d->last_freq_hz = freq_hz;
		char buf[24];

		fmt_freq(buf, sizeof(buf), freq_hz);
		lv_label_set_text(d->freq_label, buf);
	}

	/* Squelch state derived from noise byte, against whatever the menu set */
	bool sq_open = (noise < ui_get_squelch_threshold());

	lv_label_set_text(d->sq_open_label,
			  sq_open ? "[ OPEN ]" : "[ SQUELCH ]");
	lv_obj_set_style_text_color(d->sq_open_label,
				    sq_open ? lv_color_hex(0x00CC00)
					    : lv_color_hex(0x505050),
				    LV_PART_MAIN);
}
