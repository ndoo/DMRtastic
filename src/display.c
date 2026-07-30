// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <lvgl.h>

#include "view/theme.h"
#include "app.h"
#include <drivers/input/kbd_matrix_shared_bus.h>

LOG_MODULE_REGISTER(app_display, LOG_LEVEL_INF);

/** LVGL thread entry: init the display/theme, then drive the UI tick loop. */
static void lvgl_thread(void *a, void *b, void *c)
{
	const struct device *keypad = DEVICE_DT_GET(DT_NODELABEL(keypad));

	theme_init();

	lv_display_t *disp = lv_display_get_default();

	if (disp == NULL) {
		/* Display device failed init; UI is lost, radio keeps running. */
		LOG_ERR("no default LVGL display registered; UI thread exiting");
		return;
	}

	lv_theme_t *th = lv_theme_default_init(disp,
					       theme_colors()->accent_primary,
					       theme_colors()->accent_secondary,
					       true, LV_FONT_DEFAULT);
	lv_display_set_theme(disp, th);

	app_init();

	while (1) {
		/* Shares GPIOs with the LCD bus — must stay on this thread. */
		int krc = kbd_matrix_shared_bus_scan(keypad);
		if (krc != 0) {
			LOG_ERR("keypad scan failed: %d", krc);
		}
		app_tick();
		lv_timer_handler();
		k_msleep(50);
	}
}

K_THREAD_DEFINE(lvgl_tid, 8192, lvgl_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
