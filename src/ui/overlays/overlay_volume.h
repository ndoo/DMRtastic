// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Volume overlay — transient popup shown when volume changes; created once, never deleted.
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_OVERLAY_VOLUME_H_
#define DMRTASTIC_UI_OVERLAY_VOLUME_H_

#include <stdint.h>

/** Builds the hidden volume overlay panel; call once from ui_init(). */
void overlay_volume_create(void);

/** Shows the overlay at pct and resets the 2 s auto-dismiss timer. */
void overlay_volume_show(uint8_t pct);
void overlay_volume_hide(void);

#endif /* DMRTASTIC_UI_OVERLAY_VOLUME_H_ */
