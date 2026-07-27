// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Shared color palette; screens read colors via theme_colors() instead of hardcoding.
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_THEME_H_
#define DMRTASTIC_UI_THEME_H_

#include <lvgl.h>

typedef struct {
	lv_color_t bg;              /* screen / global background */
	lv_color_t surface;         /* status bar, popup panels, list containers */
	lv_color_t surface_alt;     /* dividers, unfilled bar tracks, disabled text */
	lv_color_t border;          /* popup borders */
	lv_color_t text_primary;    /* primary label text */
	lv_color_t text_secondary;  /* muted/secondary label text */
	lv_color_t accent_primary;  /* primary interactive accent (theme default) */
	lv_color_t selection_bg;    /* selected menu-row background */
	lv_color_t accent_secondary;/* secondary interactive accent (theme default) */
	lv_color_t status_success;  /* squelch-open indicator */
	lv_color_t status_warning;  /* scan indicator */
	lv_color_t status_error;    /* TX indicator */
} theme_colors_t;

/** Populates the compiled-in default palette; call once before ui_init(). */
void theme_init(void);

const theme_colors_t *theme_colors(void);

#endif /* DMRTASTIC_UI_THEME_H_ */
