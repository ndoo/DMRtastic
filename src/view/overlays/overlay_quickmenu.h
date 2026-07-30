// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Quick menu overlay — floating shortcut list, triggered by SK1 long-press (rows are placeholders).
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_OVERLAY_QUICKMENU_H_
#define DMRTASTIC_UI_OVERLAY_QUICKMENU_H_

#include <stdbool.h>

/** Builds the hidden quick-menu panel; call once from app_init(). */
void overlay_quickmenu_create(void);

/** Shows the panel and focuses its first row. */
void overlay_quickmenu_show(void);
void overlay_quickmenu_hide(void);

/** True while visible; ui.c uses this so Back closes the overlay instead of the frame underneath. */
bool overlay_quickmenu_is_active(void);

#endif /* DMRTASTIC_UI_OVERLAY_QUICKMENU_H_ */
