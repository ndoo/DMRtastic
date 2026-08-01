// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "screen_zones.h"
#include "../status_bar.h"
#include "../theme.h"
#include "../fonts/fonts.h"
#include "app.h"
#include "controller/zone_controller.h"
#include "model/codeplug.h"

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

/* Stored via lv_obj_set_user_data so widget pointers are scoped to screen lifetime. */
typedef struct {
	lv_obj_t *name_label;
	lv_obj_t *count_label;
	lv_obj_t *total_label;
	/*
	 * zone_controller's current slot as of the last paint; -2 forces the first update()
	 * to paint unconditionally (-1 is itself a valid "no in-use zone" value from the
	 * controller).
	 */
	int shown_slot;
} zones_data_t;

/** Builds the Zones screen's widgets and populates the initial zone from the controller. */
lv_obj_t *screen_zones_create(lv_obj_t *parent)
{
	lv_obj_t *scr = lv_obj_create(parent);
	lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	zones_data_t *d = lv_malloc(sizeof(*d));
	__ASSERT_NO_MSG(d != NULL);
	d->shown_slot = -2;
	lv_obj_set_user_data(scr, d);

	/* Zone slot + name — top */
	d->name_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->name_label, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_label_set_text(d->name_label, "---");
	lv_obj_align(d->name_label, LV_ALIGN_TOP_MID, 0, 10);

	/* Channel-membership count — large, centre */
	d->count_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->count_label, theme_colors()->text_primary, LV_PART_MAIN);
	lv_obj_set_style_text_font(d->count_label, &UI_FONT_LARGE, LV_PART_MAIN);
	lv_label_set_text(d->count_label, "--");
	lv_obj_align(d->count_label, LV_ALIGN_CENTER, 0, 0);

	/* Total in-use zone count — bottom-left */
	d->total_label = lv_label_create(scr);
	lv_obj_set_style_text_color(d->total_label, theme_colors()->text_secondary, LV_PART_MAIN);
	lv_label_set_text(d->total_label, "");
	lv_obj_align(d->total_label, LV_ALIGN_BOTTOM_LEFT, 6, -6);

	screen_zones_update(scr);

	return scr;
}

void screen_zones_destroy(lv_obj_t *screen)
{
	zones_data_t *d = lv_obj_get_user_data(screen);

	lv_free(d);
	lv_obj_delete(screen);
}

/** Repaints from zone_controller's cache; widgets only touched when the slot changes. */
void screen_zones_update(lv_obj_t *screen)
{
	zones_data_t *d = lv_obj_get_user_data(screen);

	/* Unconditional, like screen_fm_channel_update()'s own trailing status_bar_set_mode()
	 * call -- must run even when the slot hasn't changed, not just when the widgets
	 * below get repainted.
	 */
	status_bar_set_mode("ZN");

	int slot = zone_controller_get_current_slot();

	if (slot == d->shown_slot) {
		return;
	}
	d->shown_slot = slot;

	struct cp_zone z;
	int channels_per_zone;
	char buf[24];

	if (!zone_controller_get_current(&z, &channels_per_zone)) {
		lv_label_set_text(d->name_label, "no zone");
		lv_label_set_text(d->count_label, "");
		lv_label_set_text(d->total_label, "");
		return;
	}

	snprintf(buf, sizeof(buf), "%02d %.16s", slot, z.name);
	lv_label_set_text(d->name_label, buf);

	snprintf(buf, sizeof(buf), "%d ch", zone_controller_get_channel_count());
	lv_label_set_text(d->count_label, buf);

	snprintf(buf, sizeof(buf), "%d zones", zone_controller_get_count());
	lv_label_set_text(d->total_label, buf);
}

/** Forwards a step to the controller; see zone_controller_step(). */
void screen_zones_step(lv_obj_t *screen, bool up)
{
	(void)screen; /* controller state is a static singleton -- only one Zones screen exists */
	zone_controller_step(up);
}
