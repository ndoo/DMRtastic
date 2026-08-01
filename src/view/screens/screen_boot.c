// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "screen_boot.h"
#include "../theme.h"
#include "app.h"

#include <lvgl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define BOOT_DWELL_MS 2000

#define APP_NAME_STR    "DMRtastic"
#define APP_VERSION_STR "v0.1.0-dev"

static lv_timer_t *s_boot_timer;

static void boot_timer_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);
	app_switch_screen(SCREEN_FM_VFO);
}

/** Builds the boot splash and arms the auto-advance timer to SCREEN_FM_VFO. */
lv_obj_t *screen_boot_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *name = lv_label_create(scr);
	lv_label_set_text(name, APP_NAME_STR);
	lv_obj_set_style_text_color(name, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_align(name, LV_ALIGN_CENTER, 0, -10);

	lv_obj_t *ver = lv_label_create(scr);
	lv_label_set_text(ver, APP_VERSION_STR);
	lv_obj_set_style_text_color(ver, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_obj_align(ver, LV_ALIGN_CENTER, 0, 8);

	s_boot_timer = lv_timer_create(boot_timer_cb, BOOT_DWELL_MS, NULL);
	lv_timer_set_repeat_count(s_boot_timer, 1);

	return scr;
}

/** Cancels the auto-advance timer (if still pending) and deletes the screen. */
void screen_boot_destroy(lv_obj_t *screen)
{
	if (s_boot_timer) {
		lv_timer_delete(s_boot_timer);
		s_boot_timer = NULL;
	}
	lv_obj_delete(screen);
}

void screen_boot_update(lv_obj_t *screen)
{
	ARG_UNUSED(screen);
}
