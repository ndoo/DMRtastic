// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Boot splash screen: shows name/version for 2 s then auto-advances to
 * SCREEN_FM_VFO. Internal header — include only from src/ui/.
 */

#ifndef DMRTASTIC_UI_SCREEN_BOOT_H_
#define DMRTASTIC_UI_SCREEN_BOOT_H_

#include <lvgl.h>

/** Builds the boot splash; auto-advances to SCREEN_FM_VFO via lv_timer after 2 s. */
lv_obj_t *screen_boot_create(lv_obj_t *parent);

/** Cancels the pending auto-advance timer and deletes the screen. */
void      screen_boot_destroy(lv_obj_t *screen);
void      screen_boot_update(lv_obj_t *screen);

#endif /* DMRTASTIC_UI_SCREEN_BOOT_H_ */
