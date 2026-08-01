/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * Custom LVGL bitmap fonts (regenerated via tools/gen_fonts.py) replacing the
 * default LVGL Montserrat fonts on the HX8353E LCD. See LICENSE-Roboto.txt
 * (SIL OFL 1.1) for font licensing.
 */

#ifndef DMRTASTIC_UI_FONTS_H_
#define DMRTASTIC_UI_FONTS_H_

#include <lvgl.h>

LV_FONT_DECLARE(font_roboto_12);
LV_FONT_DECLARE(font_roboto_22);

/*
 * Single naming point for "which font is the UI's default/large role" — swap
 * candidates here instead of grepping every call site. Objects parented to
 * lv_layer_top() (overlay_volume.c, overlay_quickmenu.c) don't inherit the
 * theme's default font and must set UI_FONT_DEFAULT explicitly.
 */
#define UI_FONT_DEFAULT font_roboto_12
#define UI_FONT_LARGE   font_roboto_22

/*
 * Single shared calculation point for "how tall should a fixed-height element holding one
 * line of this font be" — every row/bar that used to hardcode a pixel height should call this
 * instead, so they all stay in sync when a font size above changes. lv_font_get_line_height()
 * is a direct struct field read (no meaningful runtime cost), so call this wherever needed
 * rather than caching the result.
 */
static inline int32_t ui_font_row_height(const lv_font_t *font)
{
	return lv_font_get_line_height(font) + 4;
}

#endif /* DMRTASTIC_UI_FONTS_H_ */
