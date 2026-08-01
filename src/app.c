// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/*
 * Screen manager — static top-level frames, meshtastic-device-ui convention.
 * All LVGL access happens on the LVGL thread; see app.h for the action-queue boundary.
 */

#include "app.h"
#include "view/theme.h"
#include "view/status_bar.h"
#include "view/overlays/overlay_volume.h"
#include "view/overlays/overlay_quickmenu.h"
#include "view/screens/screen_boot.h"
#include "view/screens/screen_fm_vfo.h"
#include "view/screens/screen_fm_channel.h"
#include "view/screens/screen_settings.h"
#include "controller/settings_controller.h"
#include "controller/fm_vfo_controller.h"
#include "controller/channel_controller.h"

#include "model/battery.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <lvgl_input_device.h>

LOG_MODULE_REGISTER(app_ui, LOG_LEVEL_INF);

/* ---------- Frame ops table -----------------------------------------------
 * SCREEN_BOOT has no entry -- one-shot transient, handled by app_init()/app_switch_screen(), not
 * the static-frame model.
 */

typedef struct {
	lv_obj_t *(*create)(lv_obj_t *parent);
	void (*update)(lv_obj_t *screen);
	void (*handle_action)(lv_obj_t *screen, ui_action_t action);
} frame_ops_t;

/** OK opens Settings; Up/Down and the encoder both step the RX frequency; digit keys drive
 * direct-frequency entry (no separate "enter mode" keypress) and Star backspaces the
 * entry's last digit.
 */
static void fm_vfo_handle_action(lv_obj_t *screen, ui_action_t action)
{
	switch (action) {
	case UI_ACTION_OK:
		app_push_screen(SCREEN_SETTINGS);
		break;
	case UI_ACTION_UP:
	case UI_ACTION_ENCODER_CW:
		screen_fm_vfo_step(screen, true);
		break;
	case UI_ACTION_DOWN:
	case UI_ACTION_ENCODER_CCW:
		screen_fm_vfo_step(screen, false);
		break;
	case UI_ACTION_KEY_0:
	case UI_ACTION_KEY_1:
	case UI_ACTION_KEY_2:
	case UI_ACTION_KEY_3:
	case UI_ACTION_KEY_4:
	case UI_ACTION_KEY_5:
	case UI_ACTION_KEY_6:
	case UI_ACTION_KEY_7:
	case UI_ACTION_KEY_8:
	case UI_ACTION_KEY_9:
		screen_fm_vfo_entry_digit(screen, action - UI_ACTION_KEY_0);
		break;
	case UI_ACTION_KEY_STAR:
		screen_fm_vfo_entry_backspace(screen);
		break;
	default:
		break;
	}
}

/** OK opens Settings, or commits an in-progress direct channel-number entry if one is active
 * (Milestone 3d). Up/Down and the encoder step to the next/previous in-use codeplug channel.
 * Digit keys drive direct channel-number entry, same no-separate-"enter mode" convention as
 * FM VFO's frequency entry; no Star/backspace here -- this entry has no backspace, only
 * commit or cancel.
 */
static void fm_channel_handle_action(lv_obj_t *screen, ui_action_t action)
{
	switch (action) {
	case UI_ACTION_OK:
		if (channel_controller_entry_active()) {
			screen_fm_channel_entry_commit(screen);
		} else {
			app_push_screen(SCREEN_SETTINGS);
		}
		break;
	case UI_ACTION_UP:
	case UI_ACTION_ENCODER_CW:
		screen_fm_channel_step(screen, true);
		break;
	case UI_ACTION_DOWN:
	case UI_ACTION_ENCODER_CCW:
		screen_fm_channel_step(screen, false);
		break;
	case UI_ACTION_KEY_0:
	case UI_ACTION_KEY_1:
	case UI_ACTION_KEY_2:
	case UI_ACTION_KEY_3:
	case UI_ACTION_KEY_4:
	case UI_ACTION_KEY_5:
	case UI_ACTION_KEY_6:
	case UI_ACTION_KEY_7:
	case UI_ACTION_KEY_8:
	case UI_ACTION_KEY_9:
		screen_fm_channel_entry_digit(screen, action - UI_ACTION_KEY_0);
		break;
	default:
		break;
	}
}

static const frame_ops_t frame_ops[SCREEN_COUNT] = {
	[SCREEN_FM_VFO] = {screen_fm_vfo_create, screen_fm_vfo_update, fm_vfo_handle_action},
	[SCREEN_FM_CHANNEL] = {screen_fm_channel_create, screen_fm_channel_update,
			       fm_channel_handle_action},
	[SCREEN_SETTINGS] = {settings_controller_create_screen, screen_settings_update,
			     screen_settings_handle_action},
	/* stubs — not implemented */
	[SCREEN_DMR_VFO] = {NULL, NULL, NULL},
	[SCREEN_DMR_CHANNEL] = {NULL, NULL, NULL},
	[SCREEN_CONTACTS] = {NULL, NULL, NULL},
	[SCREEN_ZONES] = {NULL, NULL, NULL},
};

/* ---------- Frame state -----------------------------------------------------
 * s_frame_obj[id] is set once a frame is created (never again); s_frame_stack holds Back history as
 * plain IDs -- objects live forever, nothing to destroy on pop.
 */

static lv_obj_t *s_frame_obj[SCREEN_COUNT];

#define UI_FRAME_STACK_DEPTH 4

static screen_id_t s_frame_stack[UI_FRAME_STACK_DEPTH];
static int s_frame_top = -1;

static lv_obj_t *s_boot_obj; /* one-shot transient; NULL once torn down */

/* Persistent containers, parented to the LVGL screen. */
static lv_obj_t *s_status_bar_obj;
static lv_obj_t *s_content;

/* ---------- Shared input group (meshtastic-device-ui convention) ------------
 * One lv_group_t serves every group-navigable widget, fed by the keypad indev only, attached only
 * while something with group members is on top.
 */

static lv_group_t *s_input_group;
static lv_indev_t *s_keypad_indev;
static bool s_indev_group_attached;

/** Attaches the shared input group to the keypad indev only while Settings or the quick-menu is on
 * top.
 */
static void update_indev_group_attachment(void)
{
	bool need_group = (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_SETTINGS) ||
			  overlay_quickmenu_is_active();

	if (need_group == s_indev_group_attached) {
		return;
	}

	lv_indev_set_group(s_keypad_indev, need_group ? s_input_group : NULL);
	if (need_group) {
		/* The OK press that opened this screen may still be mid-air (queued PRESS, no
		 * RELEASE yet) -- without this, LVGL fires a stray RELEASE on whatever's now
		 * focused, descending into tab content instead of the tab bar.
		 */
		lv_indev_wait_release(s_keypad_indev);
	}
	s_indev_group_attached = need_group;
}

/* ---------- Event queue (RTOS → LVGL thread boundary) ------------------- */

#define UI_EVENT_QUEUE_LEN 8

K_MSGQ_DEFINE(ui_events, sizeof(ui_action_t), UI_EVENT_QUEUE_LEN, 1);

void app_post_action(ui_action_t action)
{
	/* Non-blocking: drop if queue full rather than block the caller. */
	(void)k_msgq_put(&ui_events, &action, K_NO_WAIT);
}

/* Depth-1, overwrite-latest: the pot reports a continuous position, so only the latest value before
 * app_tick() matters.
 */
K_MSGQ_DEFINE(ui_vol_events, sizeof(uint16_t), 1, 1);

/* vol_axis_ch's out-min/out-max mirror in-min/in-max exactly (board DTS). */
#define VOL_AXIS_MIN  DT_PROP(DT_NODELABEL(vol_axis_ch), in_min)
#define VOL_AXIS_MAX  DT_PROP(DT_NODELABEL(vol_axis_ch), in_max)
#define VOL_AXIS_SPAN (VOL_AXIS_MAX - VOL_AXIS_MIN)

/* ~7% of the calibrated span per VOL_UP/VOL_DN key press. */
#define VOLUME_STEP_RAW (VOL_AXIS_SPAN * 7 / 100)

/** Clamps and overwrite-latest posts a raw volume-pot reading for app_tick() to apply. */
void app_post_volume_abs(uint16_t raw)
{
	if (raw > VOL_AXIS_MAX) {
		raw = VOL_AXIS_MAX;
	} else if (raw < VOL_AXIS_MIN) {
		raw = VOL_AXIS_MIN;
	}
	(void)k_msgq_purge(&ui_vol_events);
	(void)k_msgq_put(&ui_vol_events, &raw, K_NO_WAIT);
}

/* ---------- 200 ms update timer ----------------------------------------- */

/* Reuses this 200ms poll instead of a separate k_timer: it already runs on
 * the LVGL thread regardless of frame, avoiding new cross-thread sync.
 */
static int64_t s_last_activity_ms;
static bool s_backlight_dimmed;

/** Applies either the dimmed off-level or full brightness to the backlight PWM. */
static void backlight_apply_dim(bool dim)
{
	uint8_t pct = dim ? settings_get_backlight_off_pct() : settings_get_brightness_pct();
	const struct device *disp = DEVICE_DT_GET(DT_NODELABEL(hx8353e));
	int rc = display_set_brightness(disp, (uint8_t)DIV_ROUND_CLOSEST((uint32_t)pct * 255, 100));

	if (rc < 0) {
		LOG_WRN("display_set_brightness(%u%%) failed: %d", pct, rc);
	}
	s_backlight_dimmed = dim;
}

/** Resets the idle timer and undims the backlight if it was dimmed. */
static void ui_note_activity(void)
{
	s_last_activity_ms = k_uptime_get();
	if (s_backlight_dimmed) {
		backlight_apply_dim(false);
	}
}

/** 200 ms tick: refreshes the status bar and active frame, and applies the screen-dim timeout. */
static void update_timer_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);
	battery_poll();
	status_bar_update();
	if (s_frame_top >= 0) {
		screen_id_t id = s_frame_stack[s_frame_top];

		if (frame_ops[id].update) {
			frame_ops[id].update(s_frame_obj[id]);
		}
	}

	uint8_t screen_timeout_s = settings_get_screen_timeout_s();

	if (screen_timeout_s > 0 && !s_backlight_dimmed &&
	    (k_uptime_get() - s_last_activity_ms) >= (int64_t)screen_timeout_s * 1000) {
		backlight_apply_dim(true);
	}
}

/* ---------- Action dispatch --------------------------------------------- */

static uint16_t s_volume_raw = (VOL_AXIS_MIN + VOL_AXIS_MAX) / 2; /* runtime volume state */

/** Rescales the pot's raw ADC reading linearly to 0-100% for the driver's volume API. */
static void radio_set_volume_pct(uint16_t raw)
{
	uint8_t pct =
		(uint8_t)DIV_ROUND_CLOSEST((uint32_t)(raw - VOL_AXIS_MIN) * 100, VOL_AXIS_SPAN);
	int rc = radio_state_set_volume(pct);

	if (rc < 0) {
		LOG_WRN("set_volume(%u) failed: %d", pct, rc);
	}
}

/* 11-point piecewise-linear reverse-mapping LUTs (raw-span fraction -> display %);
 * selected at compile time by the AT1846S node's volume-taper DT property.
 */
static const uint8_t taper_lut_linear[11] = {
	0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
};

/* Standard "A" (audio/log) taper, ~14% output at 50% rotation on this board;
 * modeled as two linear segments rather than a smooth curve.
 */
static const uint8_t taper_lut_audio_a[11] = {
	0, 42, 55, 60, 66, 72, 77, 83, 89, 94, 100,
};

#if DT_ENUM_HAS_VALUE(DT_NODELABEL(at1846s), volume_taper, audio_a)
static const uint8_t *const s_volume_taper_lut = taper_lut_audio_a;
#else
static const uint8_t *const s_volume_taper_lut = taper_lut_linear;
#endif

/** Maps a raw pot reading to a display percent via the selected taper LUT, interpolating between
 * checkpoints.
 */
static uint8_t volume_display_pct(uint16_t raw)
{
	if (raw >= VOL_AXIS_MAX) {
		return s_volume_taper_lut[10];
	}

	/* Scale by 10 before dividing by span so index and in-segment fraction
	 * come from one calculation, without requiring span to be a multiple of 10.
	 */
	uint32_t pos10 = (uint32_t)(raw - VOL_AXIS_MIN) * 10;
	uint32_t idx = pos10 / VOL_AXIS_SPAN;
	uint32_t rem = pos10 % VOL_AXIS_SPAN;
	uint8_t lo = s_volume_taper_lut[idx];
	uint8_t hi = s_volume_taper_lut[idx + 1];

	return (uint8_t)(lo + DIV_ROUND_CLOSEST((hi - lo) * rem, VOL_AXIS_SPAN));
}

/** Sets hardware volume from a raw pot reading and shows the volume overlay. */
static void apply_volume(uint16_t raw)
{
	s_volume_raw = raw;
	radio_set_volume_pct(raw);
	app_overlay_volume_show(volume_display_pct(raw));
}

/** Routes a drained ui_action_t to the overlay, active frame, or global handler. */
static void dispatch_action(ui_action_t action)
{
	switch (action) {
	case UI_ACTION_BACK:
		/* Priority: quick-menu overlay > tabview row level > in-progress digit
		 * entry (either screen) > VFO<->Channel root toggle > leave the frame;
		 * each check only fires if the one above it doesn't apply.
		 */
		if (overlay_quickmenu_is_active()) {
			overlay_quickmenu_hide();
		} else if (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_SETTINGS &&
			   screen_settings_in_rows_level()) {
			screen_settings_exit_rows_level();
		} else if (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_FM_VFO &&
			   fm_vfo_controller_entry_active()) {
			screen_fm_vfo_entry_cancel(s_frame_obj[SCREEN_FM_VFO]);
		} else if (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_FM_CHANNEL &&
			   channel_controller_entry_active()) {
			screen_fm_channel_entry_cancel(s_frame_obj[SCREEN_FM_CHANNEL]);
		} else if (s_frame_top == 0 && s_frame_stack[0] == SCREEN_FM_VFO) {
			/* Root of FM VFO, nothing to pop -- a bare Back short-press
			 * toggles to Channel mode instead (Milestone 3c).
			 */
			app_switch_screen(SCREEN_FM_CHANNEL);
		} else if (s_frame_top == 0 && s_frame_stack[0] == SCREEN_FM_CHANNEL) {
			app_switch_screen(SCREEN_FM_VFO);
		} else {
			app_pop_screen();
		}
		break;
	case UI_ACTION_VOL_UP:
		apply_volume(s_volume_raw <= VOL_AXIS_MAX - VOLUME_STEP_RAW
				     ? s_volume_raw + VOLUME_STEP_RAW
				     : VOL_AXIS_MAX);
		break;
	case UI_ACTION_VOL_DN:
		apply_volume(s_volume_raw >= VOL_AXIS_MIN + VOLUME_STEP_RAW
				     ? s_volume_raw - VOLUME_STEP_RAW
				     : VOL_AXIS_MIN);
		break;
	case UI_ACTION_OK:
	case UI_ACTION_UP:
	case UI_ACTION_DOWN:
	case UI_ACTION_ENCODER_CW:
	case UI_ACTION_ENCODER_CCW:
	case UI_ACTION_KEY_0:
	case UI_ACTION_KEY_1:
	case UI_ACTION_KEY_2:
	case UI_ACTION_KEY_3:
	case UI_ACTION_KEY_4:
	case UI_ACTION_KEY_5:
	case UI_ACTION_KEY_6:
	case UI_ACTION_KEY_7:
	case UI_ACTION_KEY_8:
	case UI_ACTION_KEY_9:
	case UI_ACTION_KEY_STAR:
		/* The quick-menu's own row nav/selection runs through the shared lv_group indev --
		 * skip forwarding here too, or every press double-handles (e.g. Up/Down also steps
		 * the VFO underneath).
		 */
		if (s_frame_top >= 0 && !overlay_quickmenu_is_active()) {
			screen_id_t id = s_frame_stack[s_frame_top];

			if (frame_ops[id].handle_action) {
				frame_ops[id].handle_action(s_frame_obj[id], action);
			}
		}
		break;
	case UI_ACTION_PTT:
		status_bar_set_tx(true);
		break;
	case UI_ACTION_PTT_RELEASE:
		status_bar_set_tx(false);
		break;
	case UI_ACTION_SK1:
		overlay_quickmenu_show();
		break;
	case UI_ACTION_SK2:
	case UI_ACTION_KEY_POUND:
		LOG_INF("action %d: not yet implemented", action);
		break;
	default:
		break;
	}
}

/* ---------- Public API -------------------------------------------------- */

/** Builds the screen, status bar, overlays, and static frames; starts the update timer. */
void app_init(void)
{
	lv_obj_t *scr = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_screen_load(scr);

	/* Status bar — fixed height strip at top */
	s_status_bar_obj = lv_obj_create(scr);
	lv_obj_set_size(s_status_bar_obj, lv_pct(100), UI_STATUS_BAR_HEIGHT);
	lv_obj_set_pos(s_status_bar_obj, 0, 0);
	lv_obj_set_style_bg_color(s_status_bar_obj, theme_colors()->surface, LV_PART_MAIN);
	lv_obj_set_style_border_width(s_status_bar_obj, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(s_status_bar_obj, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(s_status_bar_obj, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(s_status_bar_obj, LV_SCROLLBAR_MODE_OFF);
	status_bar_create(s_status_bar_obj);

	/* Content container — fills remaining height below status bar */
	int32_t scr_h = lv_obj_get_height(scr);
	s_content = lv_obj_create(scr);
	lv_obj_set_size(s_content, lv_pct(100), scr_h - UI_STATUS_BAR_HEIGHT);
	lv_obj_set_pos(s_content, 0, UI_STATUS_BAR_HEIGHT);
	lv_obj_set_style_bg_color(s_content, theme_colors()->bg, LV_PART_MAIN);
	lv_obj_set_style_border_width(s_content, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(s_content, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(s_content, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);

	/* Indevs start unattached; update_indev_group_attachment() attaches them as needed. */
	s_input_group = lv_group_create();
	lv_group_set_default(s_input_group);
	s_keypad_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_NODELABEL(lvgl_keypad)));

	/* Load settings (codeplug where mapped, else firmware defaults), apply the result
	 * to hardware, and refresh the Settings menu's value_str buffers -- before any
	 * frame is created, so the first paint already shows the right values instead of
	 * waiting for the next 200 ms update tick.
	 */
	settings_controller_init();

	/* Seeds VFO A/B from the codeplug -- before the first frame paints, same reasoning
	 * as settings_controller_init() above.
	 */
	fm_vfo_controller_init();

	/* Caches the first in-use codeplug channel for the console's "channel" command and the
	 * future FM Channel screen (Milestone 3b) -- doesn't touch the radio, see
	 * channel_controller_init()'s own doc comment for why.
	 */
	channel_controller_init();

	/* Overlays — created once, parented to lv_layer_top() */
	overlay_volume_create();
	overlay_quickmenu_create();

	/* Static top-level frames — created once, hidden until shown. */
	s_frame_obj[SCREEN_FM_VFO] = frame_ops[SCREEN_FM_VFO].create(s_content);
	lv_obj_add_flag(s_frame_obj[SCREEN_FM_VFO], LV_OBJ_FLAG_HIDDEN);
	s_frame_obj[SCREEN_FM_CHANNEL] = frame_ops[SCREEN_FM_CHANNEL].create(s_content);
	lv_obj_add_flag(s_frame_obj[SCREEN_FM_CHANNEL], LV_OBJ_FLAG_HIDDEN);
	s_frame_obj[SCREEN_SETTINGS] = frame_ops[SCREEN_SETTINGS].create(s_content);
	lv_obj_add_flag(s_frame_obj[SCREEN_SETTINGS], LV_OBJ_FLAG_HIDDEN);

	/* Start 200 ms update timer */
	lv_timer_create(update_timer_cb, 200, NULL);

	/* So the screen-dim timeout doesn't appear already-elapsed at boot. */
	s_last_activity_ms = k_uptime_get();

	/* Boot screen — one-shot transient; auto-advances to FM VFO after 2 s */
	s_boot_obj = screen_boot_create(s_content);
}

/** Drains queued actions and the latest volume reading, then re-syncs input group attachment. */
void app_tick(void)
{
	ui_action_t action;
	uint16_t vol_raw;

	while (k_msgq_get(&ui_events, &action, K_NO_WAIT) == 0) {
		ui_note_activity();
		dispatch_action(action);
	}
	if (k_msgq_get(&ui_vol_events, &vol_raw, K_NO_WAIT) == 0) {
		ui_note_activity();
		apply_volume(vol_raw);
	}
	/* Cheap enough to recompute every tick — self-heals within one ~50 ms
	 * iteration after any frame switch or quick-menu show/dismiss.
	 */
	update_indev_group_attachment();
}

/** Hides the current frame and shows id, pushing it onto the Back stack. */
void app_push_screen(screen_id_t id)
{
	if (id >= SCREEN_COUNT || frame_ops[id].create == NULL) {
		LOG_WRN("push: screen %d not implemented", id);
		return;
	}
	if (s_frame_top >= UI_FRAME_STACK_DEPTH - 1) {
		LOG_WRN("push: frame stack full");
		return;
	}

	if (s_frame_top >= 0) {
		lv_obj_add_flag(s_frame_obj[s_frame_stack[s_frame_top]], LV_OBJ_FLAG_HIDDEN);
	}

	s_frame_top++;
	s_frame_stack[s_frame_top] = id;
	lv_obj_remove_flag(s_frame_obj[id], LV_OBJ_FLAG_HIDDEN);
	/* Force a first paint now rather than waiting up to one 200 ms update_timer_cb
	 * tick -- a frame hidden behind another (e.g. Settings behind the quick menu)
	 * never gets frame_ops[].update() called while hidden, so without this its
	 * widgets can still show whatever was true when it was last topmost.
	 */
	if (frame_ops[id].update) {
		frame_ops[id].update(s_frame_obj[id]);
	}

	LOG_DBG("push frame %d (depth %d)", id, s_frame_top + 1);
}

/** Pops the Back stack, hiding the current frame and revealing the previous one. */
void app_pop_screen(void)
{
	if (s_frame_top <= 0) {
		return; /* nothing to go back to */
	}

	lv_obj_add_flag(s_frame_obj[s_frame_stack[s_frame_top]], LV_OBJ_FLAG_HIDDEN);
	s_frame_top--;
	screen_id_t id = s_frame_stack[s_frame_top];

	lv_obj_remove_flag(s_frame_obj[id], LV_OBJ_FLAG_HIDDEN);
	if (frame_ops[id].update) {
		frame_ops[id].update(s_frame_obj[id]);
	}

	LOG_DBG("pop to frame %d (depth %d)", id, s_frame_top + 1);
}

/** Replaces the base (depth-0) frame with id, clearing any Back history above it. Handles
 * the one-time boot transition (destroys the boot splash) as well as the FM VFO<->Channel
 * mode toggle (Milestone 3c), which both land here with s_frame_top already at -1 or 0.
 */
void app_switch_screen(screen_id_t id)
{
	if (s_boot_obj) {
		screen_boot_destroy(s_boot_obj);
		s_boot_obj = NULL;
	}

	if (id >= SCREEN_COUNT || s_frame_obj[id] == NULL) {
		LOG_WRN("switch: screen %d not available", id);
		return;
	}

	if (s_frame_top >= 0) {
		lv_obj_add_flag(s_frame_obj[s_frame_stack[s_frame_top]], LV_OBJ_FLAG_HIDDEN);
	}

	s_frame_top = 0;
	s_frame_stack[0] = id;
	lv_obj_remove_flag(s_frame_obj[id], LV_OBJ_FLAG_HIDDEN);
	if (frame_ops[id].update) {
		frame_ops[id].update(s_frame_obj[id]);
	}

	LOG_DBG("switch to frame %d", id);
}

/** Shows the volume overlay only if the Visual volume preference is on. */
void app_overlay_volume_show(uint8_t pct)
{
	if (settings_get_visual_volume_enabled()) {
		overlay_volume_show(pct);
	}
}
