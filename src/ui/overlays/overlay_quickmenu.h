// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Quick menu overlay — floating shortcut list.
 *
 * Triggered by SK1 long-press (see ui_input.c). Centered panel, parented to
 * lv_layer_top() so it floats above whatever screen is active. Created once
 * in ui_init() and kept hidden; never deleted while the firmware is running.
 *
 * Rows are placeholders (LOG_INF on tap) until the shortcuts they name
 * (bandwidth toggle, squelch adjust, CTCSS scan) get a real quick-access
 * path independent of the full RADIO settings menu. Auto-dismisses like
 * overlay_volume; tapping a row also dismisses it immediately.
 *
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_OVERLAY_QUICKMENU_H_
#define DMRTASTIC_UI_OVERLAY_QUICKMENU_H_

void overlay_quickmenu_create(void);
void overlay_quickmenu_show(void);
void overlay_quickmenu_hide(void);

#endif /* DMRTASTIC_UI_OVERLAY_QUICKMENU_H_ */
