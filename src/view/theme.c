// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "theme.h"

/* VS Code "Dark High Contrast" anchors (hcDark workbench defaults from hc_black.json + colorRegistry.ts fallbacks).
 * Unlike the old "2026 Dark" palette, editor.background is already pure black, so no manual clamp needed. */
#define VSC_BG             lv_color_black()          /* editor.background */
#define VSC_SURFACE        lv_color_hex(0x0C141F) /* editorWidget.background (popups/dropdowns) */
#define VSC_SURFACE_ALT    lv_color_hex(0x7C7C7C) /* editorWhitespace.foreground */
#define VSC_BORDER         lv_color_hex(0x6FC3DF) /* contrastBorder */
#define VSC_FOREGROUND     lv_color_white()        /* editor.foreground */
#define VSC_MUTED          lv_color_hex(0xB3B3B3) /* descriptionForeground */
#define VSC_ACCENT         lv_color_hex(0x21A6FF) /* textLink.foreground */
#define VSC_ACCENT_LIGHT   lv_color_hex(0xF3F518) /* editor.selectionBackground */
#define VSC_SELECTION      lv_color_hex(0xF3F518) /* editor.selectionBackground */
#define VSC_GREEN          lv_color_hex(0x89D185) /* charts.green */
#define VSC_YELLOW         lv_color_hex(0xFFD370) /* editorWarning.foreground */
#define VSC_RED            lv_color_hex(0xF48771) /* editorError.foreground */

static theme_colors_t s_theme;

static theme_colors_t theme_default(void)
{
	return (theme_colors_t){
		.bg               = VSC_BG,
		.surface          = VSC_SURFACE,
		.surface_alt      = VSC_SURFACE_ALT,
		.border           = VSC_BORDER,
		.text_primary     = VSC_FOREGROUND,
		.text_secondary   = VSC_MUTED,
		.accent_primary   = VSC_ACCENT,
		.selection_bg     = VSC_SELECTION,
		.accent_secondary = VSC_ACCENT_LIGHT,
		.status_success   = VSC_GREEN,
		.status_warning   = VSC_YELLOW,
		.status_error     = VSC_RED,
	};
}

/** Populates s_theme with the compiled-in defaults (future seam for codeplug overrides). */
void theme_init(void)
{
	s_theme = theme_default();
}

const theme_colors_t *theme_colors(void)
{
	return &s_theme;
}
