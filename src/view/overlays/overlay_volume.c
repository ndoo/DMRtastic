// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "overlay_volume.h"
#include "../theme.h"

#include <lvgl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define OVERLAY_W        140
#define OVERLAY_H         36
#define DISMISS_MS      2000

static lv_obj_t  *s_panel;
static lv_obj_t  *s_label;
static lv_obj_t  *s_bar;
static lv_timer_t *s_dismiss_timer;

static void dismiss_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);
	overlay_volume_hide();
	lv_timer_pause(s_dismiss_timer);
}

/** Builds the hidden volume overlay panel; call once from app_init(). */
void overlay_volume_create(void)
{
	lv_obj_t *layer = lv_layer_top();

	s_panel = lv_obj_create(layer);
	lv_obj_set_size(s_panel, OVERLAY_W, OVERLAY_H);
	lv_obj_align(s_panel, LV_ALIGN_BOTTOM_MID, 0, -6);
	lv_obj_set_style_bg_color(s_panel, theme_colors()->surface, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(s_panel, LV_OPA_80, LV_PART_MAIN);
	lv_obj_set_style_border_color(s_panel, theme_colors()->border, LV_PART_MAIN);
	lv_obj_set_style_border_width(s_panel, 1, LV_PART_MAIN);
	lv_obj_set_style_pad_all(s_panel, 4, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(s_panel, LV_SCROLLBAR_MODE_OFF);

	s_label = lv_label_create(s_panel);
	lv_obj_set_style_text_color(s_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_label_set_text(s_label, "VOL  0 %");
	lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, 0, 0);

	s_bar = lv_bar_create(s_panel);
	lv_obj_set_size(s_bar, lv_pct(100), 8);
	lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_bar_set_range(s_bar, 0, 100);
	lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(s_bar, theme_colors()->surface_alt, LV_PART_MAIN);
	lv_obj_set_style_bg_color(s_bar, theme_colors()->text_primary, LV_PART_INDICATOR);

	lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

	/* Timer starts paused; resumed on first show. */
	s_dismiss_timer = lv_timer_create(dismiss_cb, DISMISS_MS, NULL);
	lv_timer_pause(s_dismiss_timer);
}

/** Shows the overlay at pct and resets the 2 s auto-dismiss timer. */
void overlay_volume_show(uint8_t pct)
{
	if (pct > 100) {
		pct = 100;
	}

	char buf[16];
	snprintf(buf, sizeof(buf), "VOL  %u %%", pct);
	lv_label_set_text(s_label, buf);
	lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);

	lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
	lv_timer_reset(s_dismiss_timer);
	lv_timer_resume(s_dismiss_timer);
}

void overlay_volume_hide(void)
{
	lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}
