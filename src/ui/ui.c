// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Screen manager — static top-level frames, meshtastic-device-ui convention.
 *
 * Threading boundary: all LVGL object access is confined to the LVGL thread
 * (the one that calls ui_tick() and lv_timer_handler()). External contexts
 * post ui_action_t events via ui_post_action(); ui_tick() drains them each
 * iteration. This is the only place in the firmware where actions cross from
 * the outside world into LVGL.
 *
 * Frames (FM VFO, Settings) are created exactly once at ui_init() and never
 * destroyed — "navigation" is hiding the current frame and showing another,
 * not create()/destroy() churn. SCREEN_BOOT is the one exception: a genuine
 * one-shot transient torn down by ui_switch_screen() the first (and only)
 * time it's called.
 *
 *   ui_push_screen(id) — Green/OK: show frame id, remember current for Back
 *   ui_pop_screen()    — Back/Red: hide current frame, restore the previous
 *   ui_switch_screen()  — one-time boot -> first frame transition only
 *
 * Row/tab navigation inside a frame (Settings) is native LVGL: every row and
 * tab-bar button is a member of one shared lv_group (see ui_init()), fed by
 * the zephyr,lvgl-keypad-input indev (DTS) — Up/Down always navigate.
 * The rotary encoder deliberately has no lv_group binding (OpenGD77
 * convention): it adjusts whatever's currently focused directly, via
 * UI_ACTION_ENCODER_CW/CCW (see screen_settings_handle_action()).
 * ui_action_t's UP/DOWN/OK only still matter for frames with no group
 * members at all — currently just FM VFO's direct frequency step.
 */

#include "ui.h"
#include "theme.h"
#include "status_bar.h"
#include "overlays/overlay_volume.h"
#include "overlays/overlay_quickmenu.h"
#include "screens/screen_boot.h"
#include "screens/screen_fm_vfo.h"
#include "screens/screen_settings.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <lvgl.h>
#include <lvgl_input_device.h>
#include <stdio.h>
#include <string.h>

#include <drivers/radio/radio_transceiver.h>

LOG_MODULE_REGISTER(app_ui, LOG_LEVEL_INF);

/* ---------- Menu item definitions (defined early; frame_ops needs them) -- */

/*
 * dir is always +1 or -1 here (see menu_item_t in screen_settings.h): +1 on
 * ENTER/click or an encoder CW turn, -1 on an encoder CCW turn. Adding
 * ARRAY_SIZE before the modulo keeps the result non-negative for dir=-1
 * without a full signed-modulo helper, since |dir| is always < count.
 */

/* Cycles through a small preset table; real driver call. */
static char          s_squelch_val_buf[8] = "55";
static const uint8_t squelch_presets[] = { 30, 45, 55, 70, 85 };
static uint8_t        s_squelch_idx = 2; /* index of 55, matches the boot default */

static void squelch_cycle(int8_t dir)
{
	s_squelch_idx = (uint8_t)(((int)s_squelch_idx + dir + ARRAY_SIZE(squelch_presets)) %
				  ARRAY_SIZE(squelch_presets));

	uint8_t level = squelch_presets[s_squelch_idx];
	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx->api;
	int rc = api->set_squelch(trx, level);

	if (rc < 0) {
		LOG_WRN("set_squelch(%u) failed: %d", level, rc);
	}
	snprintf(s_squelch_val_buf, sizeof(s_squelch_val_buf), "%u", level);
	ui_set_squelch_threshold(level);
}

/* Toggles 25K/12.5K; real driver call. A 2-state toggle has no meaningful
 * "direction," so dir is ignored -- either way just flips it. */
static char s_bw_val_buf[8] = "25K";
static bool s_bw_is_25k = true;

static void bandwidth_cycle(int8_t dir)
{
	ARG_UNUSED(dir);
	s_bw_is_25k = !s_bw_is_25k;

	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx->api;
	int rc = api->set_bandwidth(trx, s_bw_is_25k ? RADIO_BW_25K : RADIO_BW_12K5);

	if (rc < 0) {
		LOG_WRN("set_bandwidth failed: %d", rc);
	}
	snprintf(s_bw_val_buf, sizeof(s_bw_val_buf), "%s", s_bw_is_25k ? "25K" : "12.5K");
	ui_set_bandwidth_str(s_bw_val_buf);
}

/* VFO Up/Down step size, cycled from the RADIO settings menu. */
static const uint32_t    step_presets_hz[]    = { 2500, 5000, 6250, 12500, 25000 };
static const char *const step_preset_labels[] = { "2.5k", "5k", "6.25k", "12.5k", "25k" };
static uint8_t            s_step_idx = 1; /* 5 kHz default */
static char               s_step_val_buf[8] = "5k";

uint32_t ui_get_step_hz(void)
{
	return step_presets_hz[s_step_idx];
}

/* Cycles through the preset step-size table. */
static void step_cycle(int8_t dir)
{
	s_step_idx = (uint8_t)(((int)s_step_idx + dir + ARRAY_SIZE(step_presets_hz)) %
			       ARRAY_SIZE(step_presets_hz));
	snprintf(s_step_val_buf, sizeof(s_step_val_buf), "%s",
		 step_preset_labels[s_step_idx]);
}

static void stub_item(int8_t dir)
{
	ARG_UNUSED(dir);
	LOG_INF("item: not yet implemented");
}

static const menu_item_t radio_items[] = {
	{ "Squelch",   squelch_cycle,   s_squelch_val_buf },
	{ "Volume",    stub_item,       "7 %"             },
	{ "Bandwidth", bandwidth_cycle, s_bw_val_buf       },
	{ "Step",      step_cycle,      s_step_val_buf     },
	{ "CTCSS/DCS", stub_item,       "Off"              },
};

static const menu_item_t display_items[] = {
	{ "Brightness",     stub_item, "100%" },
	{ "Screen timeout", stub_item, "30s"  },
};

static lv_obj_t *create_settings(lv_obj_t *parent)
{
	return screen_settings_create(parent,
				       radio_items, ARRAY_SIZE(radio_items),
				       display_items, ARRAY_SIZE(display_items));
}

/* ---------- Frame ops table -----------------------------------------------
 * SCREEN_BOOT has no entry -- one-shot transient, handled by ui_init()/ui_switch_screen(), not the static-frame model. */

typedef struct {
	lv_obj_t *(*create)(lv_obj_t *parent);
	void      (*update)(lv_obj_t *screen);
	void      (*handle_action)(lv_obj_t *screen, ui_action_t action);
} frame_ops_t;

/*
 * OK opens Settings; Up/Down keys and the encoder both step the RX
 * frequency (FM VFO has no lv_group members, so the shared keypad indev's
 * PREV/NEXT delivery here is a harmless no-op regardless of what's focused
 * elsewhere).
 */
static void fm_vfo_handle_action(lv_obj_t *screen, ui_action_t action)
{
	switch (action) {
	case UI_ACTION_OK:
		ui_push_screen(SCREEN_SETTINGS);
		break;
	case UI_ACTION_UP:
	case UI_ACTION_ENCODER_CW:
		screen_fm_vfo_step(screen, true);
		break;
	case UI_ACTION_DOWN:
	case UI_ACTION_ENCODER_CCW:
		screen_fm_vfo_step(screen, false);
		break;
	default:
		break;
	}
}

static const frame_ops_t frame_ops[SCREEN_COUNT] = {
	[SCREEN_FM_VFO]      = { screen_fm_vfo_create, screen_fm_vfo_update,
				  fm_vfo_handle_action                          },
	[SCREEN_SETTINGS]    = { create_settings,      screen_settings_update,
				  screen_settings_handle_action                 },
	/* stubs — not implemented */
	[SCREEN_FM_CHANNEL]  = { NULL, NULL, NULL },
	[SCREEN_DMR_VFO]     = { NULL, NULL, NULL },
	[SCREEN_DMR_CHANNEL] = { NULL, NULL, NULL },
	[SCREEN_CONTACTS]    = { NULL, NULL, NULL },
	[SCREEN_ZONES]       = { NULL, NULL, NULL },
};

/* ---------- Frame state -----------------------------------------------------
 * s_frame_obj[id] is set once a frame is created (never again); s_frame_stack holds Back history as plain IDs -- objects live forever, nothing to destroy on pop. */

static lv_obj_t *s_frame_obj[SCREEN_COUNT];

#define UI_FRAME_STACK_DEPTH 4

static screen_id_t s_frame_stack[UI_FRAME_STACK_DEPTH];
static int         s_frame_top = -1;

static lv_obj_t *s_boot_obj; /* one-shot transient; NULL once torn down */

/* Persistent containers, parented to the LVGL screen. */
static lv_obj_t *s_status_bar_obj;
static lv_obj_t *s_content;

/* ---------- Shared input group (meshtastic-device-ui convention) ------------
 * One lv_group_t serves every group-navigable widget, fed by the keypad indev only, attached only while something with group members is on top. */

static lv_group_t *s_input_group;
static lv_indev_t *s_keypad_indev;
static bool         s_indev_group_attached;

static void update_indev_group_attachment(void)
{
	bool need_group = (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_SETTINGS) ||
			  overlay_quickmenu_is_active();

	if (need_group == s_indev_group_attached) {
		return;
	}

	lv_indev_set_group(s_keypad_indev, need_group ? s_input_group : NULL);
	if (need_group) {
		/* The OK press that opened this screen may still be mid-air (queued PRESS, no RELEASE yet) --
		 * without this, LVGL fires a stray RELEASE on whatever's now focused, descending into tab content instead of the tab bar. */
		lv_indev_wait_release(s_keypad_indev);
	}
	s_indev_group_attached = need_group;
}

/* ---------- Event queue (RTOS → LVGL thread boundary) ------------------- */

#define UI_EVENT_QUEUE_LEN 8

K_MSGQ_DEFINE(ui_events, sizeof(ui_action_t), UI_EVENT_QUEUE_LEN, 1);

void ui_post_action(ui_action_t action)
{
	/* Non-blocking: drop if queue full rather than block the caller. */
	(void)k_msgq_put(&ui_events, &action, K_NO_WAIT);
}

/*
 * Depth 1, overwrite-latest: the volume pot reports a continuous position,
 * not a discrete event — only the most recent value before the next
 * ui_tick() matters.
 */
K_MSGQ_DEFINE(ui_vol_events, sizeof(uint16_t), 1, 1);

/* vol_axis_ch's out-min/out-max mirror in-min/in-max exactly (board DTS). */
#define VOL_AXIS_MIN  DT_PROP(DT_NODELABEL(vol_axis_ch), in_min)
#define VOL_AXIS_MAX  DT_PROP(DT_NODELABEL(vol_axis_ch), in_max)
#define VOL_AXIS_SPAN (VOL_AXIS_MAX - VOL_AXIS_MIN)

/* ~7% of the calibrated span per VOL_UP/VOL_DN key press. */
#define VOLUME_STEP_RAW (VOL_AXIS_SPAN * 7 / 100)

void ui_post_volume_abs(uint16_t raw)
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

static void update_timer_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);
	status_bar_update();
	if (s_frame_top >= 0) {
		screen_id_t id = s_frame_stack[s_frame_top];

		if (frame_ops[id].update) {
			frame_ops[id].update(s_frame_obj[id]);
		}
	}
}

/* ---------- Action dispatch --------------------------------------------- */

static uint16_t s_volume_raw = (VOL_AXIS_MIN + VOL_AXIS_MAX) / 2; /* runtime volume state */

/*
 * Hardware volume has no taper correction -- the pot's own taper is what
 * gives volume control its natural feel. Just linearly rescale the native
 * raw reading down to the 0-100 percent the driver's API expects.
 */
static void radio_set_volume_pct(uint16_t raw)
{
	uint8_t pct = (uint8_t)DIV_ROUND_CLOSEST((uint32_t)(raw - VOL_AXIS_MIN) * 100,
						  VOL_AXIS_SPAN);
	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx->api;
	int rc = api->set_volume(trx, pct);

	if (rc < 0) {
		LOG_WRN("set_volume(%u) failed: %d", pct, rc);
	}
}

/*
 * Reverse-mapping taper LUTs — 11-point piecewise-linear lookups (fraction
 * of the pot's native raw span, at checkpoints 0/10,...,10/10 ->
 * perceptually-linear display %). Which one applies is chosen at compile
 * time by the AT1846S node's volume-taper DT property (see
 * auctus,at1846s.yaml).
 */
static const uint8_t taper_lut_linear[11] = {
	0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
};

/*
 * Standard "A" (audio/log) taper: industry convention is roughly 10-15% of
 * full output at 50% rotation (this board measured ~14% on-hardware),
 * modeled as two linear segments meeting at that point rather than a
 * smooth analytic curve — matching how these pots are actually
 * manufactured (two overlapping resistive tracks).
 */
static const uint8_t taper_lut_audio_a[11] = {
	0, 42, 55, 60, 66, 72, 77, 83, 89, 94, 100,
};

#if DT_ENUM_HAS_VALUE(DT_NODELABEL(at1846s), volume_taper, audio_a)
static const uint8_t *const s_volume_taper_lut = taper_lut_audio_a;
#else
static const uint8_t *const s_volume_taper_lut = taper_lut_linear;
#endif

static uint8_t volume_display_pct(uint16_t raw)
{
	if (raw >= VOL_AXIS_MAX) {
		return s_volume_taper_lut[10];
	}

	/*
	 * Scale the raw-minus-min offset by 10 *before* dividing by the span,
	 * so the checkpoint index and the in-segment fraction both come out
	 * of one exact calculation -- no need for the span to itself be a
	 * multiple of 10 (it isn't: vol_axis_ch's calibrated span is 1936
	 * counts). DIV_ROUND_CLOSEST below then rounds the final interpolated
	 * value instead of the truncating '/' that used to bias every step
	 * down by up to just under 1 point.
	 */
	uint32_t pos10 = (uint32_t)(raw - VOL_AXIS_MIN) * 10;
	uint32_t idx = pos10 / VOL_AXIS_SPAN;
	uint32_t rem = pos10 % VOL_AXIS_SPAN;
	uint8_t lo = s_volume_taper_lut[idx];
	uint8_t hi = s_volume_taper_lut[idx + 1];

	return (uint8_t)(lo + DIV_ROUND_CLOSEST((hi - lo) * rem, VOL_AXIS_SPAN));
}

static void apply_volume(uint16_t raw)
{
	s_volume_raw = raw;
	radio_set_volume_pct(raw);
	ui_overlay_volume_show(volume_display_pct(raw));
}

/*
 * Squelch/bandwidth echo state — the driver has no read-back for either, so
 * whatever the RADIO settings menu last set is the only source of truth.
 * 55 matches main.c's VHF_SQUELCH_TH boot default (not structurally linked;
 * acceptable until a real settings-persistence layer exists).
 */
static uint8_t s_squelch_threshold = 55;
static char    s_bw_str[8] = "25K";

uint8_t ui_get_squelch_threshold(void)
{
	return s_squelch_threshold;
}

void ui_set_squelch_threshold(uint8_t level)
{
	s_squelch_threshold = level;
}

const char *ui_get_bandwidth_str(void)
{
	return s_bw_str;
}

void ui_set_bandwidth_str(const char *bw)
{
	strncpy(s_bw_str, bw, sizeof(s_bw_str) - 1);
	s_bw_str[sizeof(s_bw_str) - 1] = '\0';
}

static void dispatch_action(ui_action_t action)
{
	switch (action) {
	case UI_ACTION_BACK:
		/*
		 * Priority: quick-menu overlay (no touchscreen to dismiss it
		 * with) > a tabview's row level (ascend to the tab level
		 * first, per-tabview two-level hierarchy) > leave the frame
		 * entirely. Each check only fires if the thing above it
		 * doesn't apply, so Back always does exactly one step.
		 */
		if (overlay_quickmenu_is_active()) {
			overlay_quickmenu_hide();
		} else if (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_SETTINGS &&
			   screen_settings_in_rows_level()) {
			screen_settings_exit_rows_level();
		} else {
			ui_pop_screen();
		}
		break;
	case UI_ACTION_VOL_UP:
		apply_volume(s_volume_raw <= VOL_AXIS_MAX - VOLUME_STEP_RAW
				     ? s_volume_raw + VOLUME_STEP_RAW : VOL_AXIS_MAX);
		break;
	case UI_ACTION_VOL_DN:
		apply_volume(s_volume_raw >= VOL_AXIS_MIN + VOLUME_STEP_RAW
				     ? s_volume_raw - VOLUME_STEP_RAW : VOL_AXIS_MIN);
		break;
	case UI_ACTION_OK:
	case UI_ACTION_UP:
	case UI_ACTION_DOWN:
	case UI_ACTION_ENCODER_CW:
	case UI_ACTION_ENCODER_CCW:
		if (s_frame_top >= 0) {
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
	case UI_ACTION_KEY_POUND:
		LOG_INF("action %d: not yet implemented", action);
		break;
	default:
		break;
	}
}

/* ---------- Public API -------------------------------------------------- */

void ui_init(void)
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
	lv_obj_set_style_bg_color(s_status_bar_obj, theme_colors()->surface,
				  LV_PART_MAIN);
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

	/*
	 * Shared input group — meshtastic-device-ui convention. Indevs start
	 * unattached (group = NULL, LVGL's default); update_indev_group_
	 * attachment() attaches/detaches them as frames/overlays change.
	 */
	s_input_group = lv_group_create();
	lv_group_set_default(s_input_group);
	s_keypad_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_NODELABEL(lvgl_keypad)));

	/* Overlays — created once, parented to lv_layer_top() */
	overlay_volume_create();
	overlay_quickmenu_create();

	/* Static top-level frames — created once, hidden until shown. */
	s_frame_obj[SCREEN_FM_VFO] = frame_ops[SCREEN_FM_VFO].create(s_content);
	lv_obj_add_flag(s_frame_obj[SCREEN_FM_VFO], LV_OBJ_FLAG_HIDDEN);
	s_frame_obj[SCREEN_SETTINGS] = frame_ops[SCREEN_SETTINGS].create(s_content);
	lv_obj_add_flag(s_frame_obj[SCREEN_SETTINGS], LV_OBJ_FLAG_HIDDEN);

	/* Start 200 ms update timer */
	lv_timer_create(update_timer_cb, 200, NULL);

	/* Boot screen — one-shot transient; auto-advances to FM VFO after 2 s */
	s_boot_obj = screen_boot_create(s_content);
}

void ui_tick(void)
{
	ui_action_t action;
	uint16_t vol_raw;

	while (k_msgq_get(&ui_events, &action, K_NO_WAIT) == 0) {
		dispatch_action(action);
	}
	if (k_msgq_get(&ui_vol_events, &vol_raw, K_NO_WAIT) == 0) {
		apply_volume(vol_raw);
	}
	/* Cheap enough to recompute every tick — self-heals within one ~50 ms
	 * iteration after any frame switch or quick-menu show/dismiss. */
	update_indev_group_attachment();
}

void ui_push_screen(screen_id_t id)
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

	LOG_DBG("push frame %d (depth %d)", id, s_frame_top + 1);
}

void ui_pop_screen(void)
{
	if (s_frame_top <= 0) {
		return; /* nothing to go back to */
	}

	lv_obj_add_flag(s_frame_obj[s_frame_stack[s_frame_top]], LV_OBJ_FLAG_HIDDEN);
	s_frame_top--;
	lv_obj_remove_flag(s_frame_obj[s_frame_stack[s_frame_top]], LV_OBJ_FLAG_HIDDEN);

	LOG_DBG("pop to frame %d (depth %d)", s_frame_stack[s_frame_top], s_frame_top + 1);
}

void ui_switch_screen(screen_id_t id)
{
	if (s_boot_obj) {
		screen_boot_destroy(s_boot_obj);
		s_boot_obj = NULL;
	}

	if (id >= SCREEN_COUNT || s_frame_obj[id] == NULL) {
		LOG_WRN("switch: screen %d not available", id);
		return;
	}

	s_frame_top = 0;
	s_frame_stack[0] = id;
	lv_obj_remove_flag(s_frame_obj[id], LV_OBJ_FLAG_HIDDEN);

	LOG_DBG("switch to frame %d", id);
}

void ui_overlay_volume_show(uint8_t pct)
{
	overlay_volume_show(pct);
}
