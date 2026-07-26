// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <lvgl.h>

#include "ui/ui.h"
#include <drivers/input/kbd_matrix_shared_bus.h>

LOG_MODULE_REGISTER(app_display, LOG_LEVEL_INF);

static void lvgl_thread(void *a, void *b, void *c)
{
	const struct device *keypad = DEVICE_DT_GET(DT_NODELABEL(keypad));

	lv_display_t *disp = lv_display_get_default();
	lv_theme_t *th = lv_theme_default_init(disp,
					       lv_color_white(),
					       lv_color_black(),
					       true, LV_FONT_DEFAULT);
	lv_display_set_theme(disp, th);

	ui_init();

	while (1) {
		/*
		 * Scan the keypad matrix before touching the LCD: it borrows
		 * the same 8 GPIOs as lcd_mipi_dbi's data bus and hands them
		 * back before returning. Must stay on this thread — see
		 * kbd_matrix_shared_bus.h.
		 */
		int krc = kbd_matrix_shared_bus_scan(keypad);
		if (krc != 0) {
			LOG_ERR("keypad scan failed: %d", krc);
		}
		ui_tick();
		lv_timer_handler();
		k_msleep(50);
	}
}

K_THREAD_DEFINE(lvgl_tid, 4096, lvgl_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
