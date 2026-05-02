// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_REGISTER(app_display, LOG_LEVEL_INF);

static void lvgl_thread(void *a, void *b, void *c)
{
	lv_display_t *disp = lv_display_get_default();
	lv_theme_t *th = lv_theme_default_init(disp, lv_color_white(), lv_color_black(), true, LV_FONT_DEFAULT);
	lv_display_set_theme(disp, th);

	lv_obj_t *scr = lv_obj_create(NULL);
	lv_screen_load(scr);

	lv_obj_t *label = lv_label_create(scr);

	lv_label_set_text(label, "Hello World");
	lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

	while (1) {
		lv_timer_handler();
		k_msleep(50);
	}
}

K_THREAD_DEFINE(lvgl_tid, 4096, lvgl_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
