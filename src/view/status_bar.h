/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * Persistent status bar: mode label, RSSI bars, GPS dot (placeholder), battery label.
 * Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_STATUS_BAR_H_
#define DMRTASTIC_UI_STATUS_BAR_H_

#include <stdbool.h>
#include <lvgl.h>

#define UI_STATUS_BAR_HEIGHT 14

/** Builds the status bar widgets under parent; call once from app_init(). */
void status_bar_create(lv_obj_t *parent);

/** Refreshes the RSSI bars and battery label; call periodically. */
void status_bar_update(void);
void status_bar_set_mode(const char *mode);

/** Show/hide the TX indicator. UI feedback only — no RF is enabled by this. */
void status_bar_set_tx(bool active);

#endif /* DMRTASTIC_UI_STATUS_BAR_H_ */
