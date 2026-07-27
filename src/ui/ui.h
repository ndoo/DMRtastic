// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Public UI API — the only header other modules need to include.
 *
 * Threading model: all LVGL object access happens exclusively inside the
 * LVGL thread. External threads (radio driver callbacks, future keypad ISR)
 * post ui_action_t events via ui_post_action(); ui_tick() drains them each
 * iteration before lv_timer_handler(). Never touch LVGL objects directly
 * from outside the LVGL thread.
 */

#ifndef DMRTASTIC_UI_H_
#define DMRTASTIC_UI_H_

#include <stdint.h>

/* ---------- Screen IDs -------------------------------------------------- */

/*
 * meshtastic-device-ui convention: no modal push/pop screens. SCREEN_BOOT is
 * the one exception (a genuine one-shot transient, created then destroyed
 * exactly once at startup). Every other screen is a static top-level frame:
 * created once at ui_init() and never destroyed again, shown/hidden via
 * ui_push_screen()/ui_pop_screen() (naming kept for minimal diff, but these
 * no longer create/destroy anything past boot — see ui.c).
 *
 * IDs marked "stub" are reserved to keep enum values stable; their ops
 * entries have NULL create/update and will log a warning if shown.
 */
typedef enum {
	SCREEN_BOOT = 0,      /* splash — auto-advances to FM_VFO after 2 s   */
	SCREEN_FM_VFO,        /* FM direct-frequency operating screen          */
	SCREEN_SETTINGS,      /* Radio/Display/Info tabview (screen_settings.c)*/
	/* -- reserved stubs (not implemented yet) -- */
	SCREEN_FM_CHANNEL,    /* FM channel mode (needs codeplug)              */
	SCREEN_DMR_VFO,       /* DMR direct-frequency (needs HR-C6000 DMR)    */
	SCREEN_DMR_CHANNEL,   /* DMR channel mode (needs codeplug + HR-C6000)  */
	SCREEN_CONTACTS,      /* contact list (needs codeplug)                 */
	SCREEN_ZONES,         /* zone list (needs codeplug)                    */
	SCREEN_COUNT
} screen_id_t;

/* ---------- Input actions ----------------------------------------------- */

/*
 * Device-agnostic action codes. The input bridge (ui_input.c) translates
 * DTS gpio-keys zephyr,code values to these before posting via ui_post_action.
 *
 * OpenGD77 convention: physical Up/Down keys always *navigate* (row focus,
 * tab switching) — handled natively by LVGL's lv_group/lv_indev system (see
 * ui_init()) and never travel through ui_action_t at all. The rotary
 * encoder instead *adjusts the currently-focused setting* directly
 * (UI_ACTION_ENCODER_CW/CCW) — it deliberately has no lv_group binding, so
 * turning it never moves focus. UI_ACTION_UP/DOWN/OK still exist for
 * screens with no group members at all (FM VFO's direct frequency step,
 * still reachable via both the physical Up/Down keys and the encoder — see
 * fm_vfo_handle_action() in ui.c). Back stays an action deliberately: it
 * means "leave this frame," which doesn't generalize the way PREV/NEXT does
 * within one frame.
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

/* Called once from the LVGL thread before the timer loop starts. */
void ui_init(void);

/*
 * Called once per loop iteration, before lv_timer_handler().
 * Drains the ui_events message queue and forwards actions to the active screen.
 */
void ui_tick(void);

/* ---------- Navigation -------------------------------------------------- */

/*
 * Show a static top-level frame (Green/OK) — hides the currently-shown
 * frame and reveals the target; both already exist (created once at
 * ui_init()). No allocation, no lv_obj_create()/lv_obj_delete().
 */
void ui_push_screen(screen_id_t id);

/* Hide the current frame and restore the previous one (Back/Red). */
void ui_pop_screen(void);

/*
 * One-time bootstrap transition: destroys the (genuinely transient) boot
 * splash and shows the given static frame for the first time. Not used for
 * any other navigation — see ui_push_screen()/ui_pop_screen() for that.
 */
void ui_switch_screen(screen_id_t id);

/* ---------- Overlays ---------------------------------------------------- */

/* Show the volume overlay set to pct (0–100). Auto-dismisses after 2 s. */
void ui_overlay_volume_show(uint8_t pct);

/*
 * Write-through echo of the last squelch/bandwidth value set via the RADIO
 * settings menu — the driver has no read-back for either, so the UI is the
 * source of truth other screens (e.g. the FM VFO squelch indicator) read
 * from.
 */
uint8_t     ui_get_squelch_threshold(void);
void        ui_set_squelch_threshold(uint8_t level);
const char *ui_get_bandwidth_str(void);
void        ui_set_bandwidth_str(const char *bw);

/*
 * Current VFO step size in Hz, set via the RADIO settings menu's Step row.
 * Read by screen_fm_vfo_step() to compute the Up/Down frequency increment.
 */
uint32_t    ui_get_step_hz(void);

/* ---------- Input bridge ------------------------------------------------ */

/* Safe to call from any thread context; never from hard-ISR. */
void ui_post_action(ui_action_t action);

/*
 * Post an absolute volume position as the volume pot's native calibrated
 * ADC reading (vol_axis_ch's in-min..in-max span -- see the board DTS;
 * out-min/out-max mirror it exactly, so this is exactly what the axis
 * reports, with no intermediate rescaling). Kept native all the way
 * through so the taper LUT and hardware volume each round down from it
 * independently at their own point of use in ui.c, instead of both being
 * capped by one early rescaling choice. Overwrite-latest: only the most
 * recent value before the next ui_tick() matters. Safe to call from any
 * thread context; never from hard-ISR.
 */
void ui_post_volume_abs(uint16_t raw);

#endif /* DMRTASTIC_UI_H_ */
