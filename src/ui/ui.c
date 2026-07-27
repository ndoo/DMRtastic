// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Screen manager and navigation stack.
 *
 * Threading boundary: all LVGL object access is confined to the LVGL thread
 * (the one that calls ui_tick() and lv_timer_handler()). External contexts
 * post ui_action_t events via ui_post_action(); ui_tick() drains them each
 * iteration. This is the only place in the firmware where actions cross from
 * the outside world into LVGL.
 *
 * Navigation stack (depth UI_NAV_STACK_DEPTH):
 *   ui_push_screen(id)   — Green/OK: create new screen, push onto stack
 *   ui_pop_screen()      — Back/Red: destroy top, restore previous
 *   ui_switch_screen(id) — mode change: flush entire stack, load new screen
 *
 * Screen modules register via the screen_ops[] table. Adding a new screen:
 *   1. Add an enum value to screen_id_t in ui.h
 *   2. Add a screen_ops_t row below (NULL ops = reserved stub)
 *   3. Add screen_*.c to the build
 *
 * Menu screens share a single generic renderer (screen_menu.c); call
 * screen_menu_prepare() before ui_push_screen(SCREEN_MENU_*).
 */

#include "ui.h"
#include "theme.h"
#include "status_bar.h"
#include "overlays/overlay_volume.h"
#include "overlays/overlay_quickmenu.h"
#include "screens/screen_boot.h"
#include "screens/screen_fm_vfo.h"
#include "screens/screen_menu.h"
#include "screens/screen_device_info.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include <drivers/radio/radio_transceiver.h>

LOG_MODULE_REGISTER(app_ui, LOG_LEVEL_INF);

/* ---------- Screen ops table -------------------------------------------- */

typedef struct {
	lv_obj_t *(*create)(lv_obj_t *parent);
	void      (*destroy)(lv_obj_t *screen);
	void      (*update)(lv_obj_t *screen);
	void      (*handle_action)(lv_obj_t *screen, ui_action_t action);
} screen_ops_t;

/* Forward declarations for menu prepare helpers defined at bottom of file. */
static void prepare_menu_main(void);
static void prepare_menu_radio(void);
static void prepare_menu_display(void);

/* Wrappers that call prepare then create, so the ops table stays uniform. */
static lv_obj_t *create_menu_main(lv_obj_t *p)
{
	prepare_menu_main();
	return screen_menu_create(p);
}
static lv_obj_t *create_menu_radio(lv_obj_t *p)
{
	prepare_menu_radio();
	return screen_menu_create(p);
}
static lv_obj_t *create_menu_display(lv_obj_t *p)
{
	prepare_menu_display();
	return screen_menu_create(p);
}

/*
 * No physical key maps to UI_ACTION_MENU (no dedicated MENU key in the
 * hardware layout) — OK from the FM VFO screen opens the main menu instead.
 */
static void fm_vfo_handle_action(lv_obj_t *screen, ui_action_t action)
{
	switch (action) {
	case UI_ACTION_OK:
		ui_push_screen(SCREEN_MENU_MAIN);
		break;
	case UI_ACTION_UP:
		screen_fm_vfo_step(screen, true);
		break;
	case UI_ACTION_DOWN:
		screen_fm_vfo_step(screen, false);
		break;
	default:
		break;
	}
}

static const screen_ops_t screen_ops[SCREEN_COUNT] = {
	[SCREEN_BOOT]         = { screen_boot_create,        screen_boot_destroy,        screen_boot_update,        NULL                        },
	[SCREEN_FM_VFO]       = { screen_fm_vfo_create,       screen_fm_vfo_destroy,      screen_fm_vfo_update,      fm_vfo_handle_action        },
	[SCREEN_MENU_MAIN]    = { create_menu_main,           screen_menu_destroy,        screen_menu_update,        screen_menu_handle_action   },
	[SCREEN_MENU_RADIO]   = { create_menu_radio,          screen_menu_destroy,        screen_menu_update,        screen_menu_handle_action   },
	[SCREEN_MENU_DISPLAY] = { create_menu_display,        screen_menu_destroy,        screen_menu_update,        screen_menu_handle_action   },
	[SCREEN_DEVICE_INFO]  = { screen_device_info_create,  screen_device_info_destroy, screen_device_info_update, NULL                        },
	/* stubs — not implemented */
	[SCREEN_FM_CHANNEL]   = { NULL, NULL, NULL, NULL },
	[SCREEN_DMR_VFO]      = { NULL, NULL, NULL, NULL },
	[SCREEN_DMR_CHANNEL]  = { NULL, NULL, NULL, NULL },
	[SCREEN_CONTACTS]     = { NULL, NULL, NULL, NULL },
	[SCREEN_ZONES]        = { NULL, NULL, NULL, NULL },
};

/* ---------- Navigation stack -------------------------------------------- */

#define UI_NAV_STACK_DEPTH 8

static struct {
	screen_id_t id;
	lv_obj_t   *obj;
} nav_stack[UI_NAV_STACK_DEPTH];

static int nav_top = -1;

/* Persistent containers, parented to the LVGL screen. */
static lv_obj_t *s_status_bar_obj;
static lv_obj_t *s_content;

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
	if (nav_top >= 0 && screen_ops[nav_stack[nav_top].id].update) {
		screen_ops[nav_stack[nav_top].id].update(nav_stack[nav_top].obj);
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

/* VFO Up/Down step size, cycled from the RADIO settings menu. */
static const uint32_t    step_presets_hz[]    = { 2500, 5000, 6250, 12500, 25000 };
static const char *const step_preset_labels[] = { "2.5k", "5k", "6.25k", "12.5k", "25k" };
static uint8_t            s_step_idx = 1; /* 5 kHz default */
static char               s_step_val_buf[8] = "5k";

uint32_t ui_get_step_hz(void)
{
	return step_presets_hz[s_step_idx];
}

static void dispatch_action(ui_action_t action)
{
	switch (action) {
	case UI_ACTION_BACK:
		ui_pop_screen();
		break;
	case UI_ACTION_MENU:
		if (nav_top >= 0 && nav_stack[nav_top].id != SCREEN_MENU_MAIN) {
			ui_push_screen(SCREEN_MENU_MAIN);
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
		if (nav_top >= 0 && screen_ops[nav_stack[nav_top].id].handle_action) {
			screen_ops[nav_stack[nav_top].id].handle_action(
				nav_stack[nav_top].obj, action);
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

	/* Overlays — created once, parented to lv_layer_top() */
	overlay_volume_create();
	overlay_quickmenu_create();

	/* Start 200 ms update timer */
	lv_timer_create(update_timer_cb, 200, NULL);

	/* Boot screen — auto-advances to FM VFO after 2 s */
	ui_switch_screen(SCREEN_BOOT);
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
}

void ui_push_screen(screen_id_t id)
{
	if (id >= SCREEN_COUNT || screen_ops[id].create == NULL) {
		LOG_WRN("push: screen %d not implemented", id);
		return;
	}
	if (nav_top >= UI_NAV_STACK_DEPTH - 1) {
		LOG_WRN("push: nav stack full");
		return;
	}

	lv_obj_t *obj = screen_ops[id].create(s_content);
	nav_top++;
	nav_stack[nav_top].id  = id;
	nav_stack[nav_top].obj = obj;

	LOG_DBG("push screen %d (depth %d)", id, nav_top + 1);
}

void ui_pop_screen(void)
{
	if (nav_top < 0) {
		return;
	}

	screen_id_t id  = nav_stack[nav_top].id;
	lv_obj_t   *obj = nav_stack[nav_top].obj;

	if (screen_ops[id].destroy) {
		screen_ops[id].destroy(obj);
	}
	nav_top--;

	LOG_DBG("pop screen %d (depth %d)", id, nav_top + 1);
}

void ui_switch_screen(screen_id_t id)
{
	/* Flush the entire stack. */
	while (nav_top >= 0) {
		screen_id_t sid  = nav_stack[nav_top].id;
		lv_obj_t   *sobj = nav_stack[nav_top].obj;

		if (screen_ops[sid].destroy) {
			screen_ops[sid].destroy(sobj);
		}
		nav_top--;
	}

	if (id >= SCREEN_COUNT || screen_ops[id].create == NULL) {
		LOG_WRN("switch: screen %d not implemented", id);
		return;
	}

	lv_obj_t *obj = screen_ops[id].create(s_content);
	nav_top = 0;
	nav_stack[0].id  = id;
	nav_stack[0].obj = obj;

	LOG_DBG("switch to screen %d", id);
}

void ui_overlay_volume_show(uint8_t pct)
{
	overlay_volume_show(pct);
}

/* ---------- Menu item definitions --------------------------------------- */

static void open_menu_radio(void)   { ui_push_screen(SCREEN_MENU_RADIO);   }
static void open_menu_display(void) { ui_push_screen(SCREEN_MENU_DISPLAY); }
static void open_device_info(void)  { ui_push_screen(SCREEN_DEVICE_INFO);  }
static void stub_item(void)         { LOG_INF("item: not yet implemented"); }

static const menu_item_t main_items[] = {
	{ "Radio Settings",   open_menu_radio,   NULL  },
	{ "Display Settings", open_menu_display, NULL  },
	{ "Device Info",      open_device_info,  NULL  },
	{ "Channels",         stub_item,         "N/A" },
};

/* Cycles through a small preset table on each OK press; real driver call. */
static char          s_squelch_val_buf[8] = "55";
static const uint8_t squelch_presets[] = { 30, 45, 55, 70, 85 };
static uint8_t        s_squelch_idx = 2; /* index of 55, matches the boot default */

static void squelch_cycle(void)
{
	s_squelch_idx = (uint8_t)((s_squelch_idx + 1) % ARRAY_SIZE(squelch_presets));

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

/* Toggles 25K/12.5K on each OK press; real driver call. */
static char s_bw_val_buf[8] = "25K";
static bool s_bw_is_25k = true;

static void bandwidth_cycle(void)
{
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

/* Cycles through the preset step-size table on each OK press. */
static void step_cycle(void)
{
	s_step_idx = (uint8_t)((s_step_idx + 1) % ARRAY_SIZE(step_presets_hz));
	snprintf(s_step_val_buf, sizeof(s_step_val_buf), "%s",
		 step_preset_labels[s_step_idx]);
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

static void prepare_menu_main(void)
{
	screen_menu_prepare("MENU", main_items, ARRAY_SIZE(main_items));
}

static void prepare_menu_radio(void)
{
	screen_menu_prepare("RADIO", radio_items, ARRAY_SIZE(radio_items));
}

static void prepare_menu_display(void)
{
	screen_menu_prepare("DISPLAY", display_items, ARRAY_SIZE(display_items));
}
