/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * Public UI API — the only header other modules need to include.
 * All LVGL access is confined to the LVGL thread; post ui_action_t via app_post_action().
 */

#ifndef DMRTASTIC_APP_H_
#define DMRTASTIC_APP_H_

#include <stdbool.h>
#include <stdint.h>

/* ---------- Screen IDs -------------------------------------------------- */

/* Static top-level frames (meshtastic-device-ui convention), except SCREEN_BOOT which is a genuine
 * one-shot transient. Stub IDs reserve enum values; their ops entries have NULL create/update and
 * log a warning if shown.
 */
typedef enum {
	SCREEN_BOOT = 0, /* splash — auto-advances to FM_VFO after 2 s   */
	SCREEN_FM_VFO,   /* FM direct-frequency operating screen          */
	SCREEN_SETTINGS, /* Radio/Display/Info tabview (screen_settings.c)*/
	/* -- reserved stubs (not implemented yet) -- */
	SCREEN_FM_CHANNEL,  /* FM channel mode (needs codeplug)              */
	SCREEN_DMR_VFO,     /* DMR direct-frequency (needs HR-C6000 DMR)    */
	SCREEN_DMR_CHANNEL, /* DMR channel mode (needs codeplug + HR-C6000)  */
	SCREEN_CONTACTS,    /* contact list (needs codeplug)                 */
	SCREEN_ZONES,       /* zone list (needs codeplug)                    */
	SCREEN_COUNT
} screen_id_t;

/* ---------- Input actions ----------------------------------------------- */

/* Device-agnostic action codes; app_input.c translates DTS zephyr,code values to these.
 * Up/Down/tab navigation is native LVGL group focus, not this enum; the encoder adjusts the focused
 * setting via ENCODER_CW/CCW instead.
 */
typedef enum {
	UI_ACTION_BACK,        /* leave the current frame (Red / Back key) */
	UI_ACTION_UP,          /* non-widget-list use only (e.g. VFO step)  */
	UI_ACTION_DOWN,        /* non-widget-list use only (e.g. VFO step)  */
	UI_ACTION_OK,          /* non-widget-list use only (e.g. open Settings) */
	UI_ACTION_ENCODER_CW,  /* rotary encoder: adjust focused setting up   */
	UI_ACTION_ENCODER_CCW, /* rotary encoder: adjust focused setting down */
	UI_ACTION_VOL_UP,      /* volume up — triggers volume overlay   */
	UI_ACTION_VOL_DN,      /* volume down — triggers volume overlay */
	UI_ACTION_PTT,         /* push-to-talk pressed                  */
	UI_ACTION_SK1,         /* side key 1 (long-press = quick menu)  */
	UI_ACTION_PTT_RELEASE, /* push-to-talk released                 */
	UI_ACTION_SK2,         /* side key 2 — no behavior defined yet  */
	UI_ACTION_KEY_0,
	UI_ACTION_KEY_1,
	UI_ACTION_KEY_2,
	UI_ACTION_KEY_3,
	UI_ACTION_KEY_4,
	UI_ACTION_KEY_5,
	UI_ACTION_KEY_6,
	UI_ACTION_KEY_7,
	UI_ACTION_KEY_8,
	UI_ACTION_KEY_9,
	UI_ACTION_KEY_STAR,
	UI_ACTION_KEY_POUND,
} ui_action_t;

/* ---------- Lifecycle --------------------------------------------------- */

/** Called once from the LVGL thread before the timer loop starts. */
void app_init(void);

/** Called once per loop iteration before lv_timer_handler(); drains queued actions to the active
 * screen.
 */
void app_tick(void);

/* ---------- Navigation -------------------------------------------------- */

/** Shows frame id (already created at app_init()) and hides the current one; no alloc. */
void app_push_screen(screen_id_t id);

/** Hide the current frame and restore the previous one (Back/Red). */
void app_pop_screen(void);

/** One-time boot transition: destroys the boot splash and shows id for the first time. */
void app_switch_screen(screen_id_t id);

/* ---------- Overlays ---------------------------------------------------- */

/** Show the volume overlay set to pct (0-100). Auto-dismisses after 2 s. */
void app_overlay_volume_show(uint8_t pct);

/* Squelch/bandwidth/VFO-step/battery-unit state set via the Settings menu now lives in
 * radio_settings.h (settings_get_squelch_level(), settings_get_vfo_step_hz(),
 * settings_get_battery_unit_is_percent(), etc.) -- other screens should include that
 * header directly instead of going through app.h.
 */

/* ---------- Input bridge ------------------------------------------------ */

/** Safe to call from any thread context; never from hard-ISR. */
void app_post_action(ui_action_t action);

/** Posts an absolute volume-pot reading (raw, native ADC span); overwrite-latest. Safe from any
 * thread, never hard-ISR.
 */
void app_post_volume_abs(uint16_t raw);

#endif /* DMRTASTIC_APP_H_ */
