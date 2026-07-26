// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Quick menu overlay — context-sensitive shortcut list.
 *
 * Triggered by SK1 long-press from the FM VFO screen.
 * Items vary by active screen (bandwidth toggle, squelch adjust, CTCSS scan).
 *
 * STUB — not implemented yet. Waiting for gpio-keys DTS and keypad driver.
 *
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_OVERLAY_QUICKMENU_H_
#define DMRTASTIC_UI_OVERLAY_QUICKMENU_H_

void overlay_quickmenu_create(void);
void overlay_quickmenu_show(void);
void overlay_quickmenu_hide(void);

#endif /* DMRTASTIC_UI_OVERLAY_QUICKMENU_H_ */
