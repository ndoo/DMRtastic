// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "screen_device_info.h"

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

typedef struct {
	lv_obj_t *uptime_label;
	lv_obj_t *rssi_label;
} dev_info_data_t;

lv_obj_t *screen_device_info_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_layout(scr, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(scr, 4, LV_PART_MAIN);
	lv_obj_set_style_pad_row(scr, 2, LV_PART_MAIN);

	dev_info_data_t *d = lv_malloc(sizeof(*d));
	__ASSERT_NO_MSG(d != NULL);
	lv_obj_set_user_data(scr, d);

	static const char * const static_lines[] = {
		"DMRtastic " APP_VERSION_STR,
		"Zephyr " KERNEL_VERSION_STRING,
	};
	for (size_t i = 0; i < ARRAY_SIZE(static_lines); i++) {
		lv_obj_t *l = lv_label_create(scr);
		lv_label_set_text(l, static_lines[i]);
		lv_obj_set_style_text_color(l, lv_color_white(), LV_PART_MAIN);
	}

	d->uptime_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->uptime_label, lv_color_white(),
				    LV_PART_MAIN);

	d->rssi_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->rssi_label, lv_color_white(),
				    LV_PART_MAIN);

	screen_device_info_update(scr);

	return scr;
}

void screen_device_info_destroy(lv_obj_t *screen)
{
	lv_free(lv_obj_get_user_data(screen));
	lv_obj_delete(screen);
}

void screen_device_info_update(lv_obj_t *screen)
{
	dev_info_data_t *d = lv_obj_get_user_data(screen);

	char buf[32];
	snprintf(buf, sizeof(buf), "Uptime: %lld s",
		 k_uptime_get() / 1000LL);
	lv_label_set_text(d->uptime_label, buf);

	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx->api;
	uint8_t signal = 0, noise = 0;
	api->get_rssi(trx, &signal, &noise);

	snprintf(buf, sizeof(buf), "RSSI sig=%u noise=%u", signal, noise);
	lv_label_set_text(d->rssi_label, buf);
}
