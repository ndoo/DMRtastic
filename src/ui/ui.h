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
 * Each ID maps to a screen_ops_t row in ui.c. IDs marked "stub" are reserved
 * to keep enum values stable; their ops entries have NULL create/destroy/update
 * and will log an error if pushed.
 */
typedef enum {
	SCREEN_BOOT = 0,      /* splash — auto-advances to FM_VFO after 2 s   */
	SCREEN_FM_VFO,        /* FM direct-frequency operating screen          */
	SCREEN_MENU_MAIN,     /* top-level menu list                           */
	SCREEN_MENU_RADIO,    /* Radio Settings sub-menu                       */
	SCREEN_MENU_DISPLAY,  /* Display Settings sub-menu                     */
	SCREEN_DEVICE_INFO,   /* firmware version, uptime, raw RSSI            */
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
 * Screen handlers only ever see ui_action_t — never raw key codes or GPIOs.
 */
typedef enum {
	UI_ACTION_MENU,        /* open/close main menu                  */
	UI_ACTION_BACK,        /* pop screen (Red / Back key)           */
	UI_ACTION_UP,          /* scroll / increment                    */
	UI_ACTION_DOWN,        /* scroll / decrement                    */
	UI_ACTION_OK,          /* confirm / push child screen (Green)   */
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

/* Push a new screen; previous screen stays alive on the stack (Green/OK). */
void ui_push_screen(screen_id_t id);

/* Destroy the top screen and restore the previous one (Back/Red). */
void ui_pop_screen(void);

/*
 * Flush the entire stack and load a single screen.
 * Used for mode changes: boot → FM VFO, VFO ↔ Channel.
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
