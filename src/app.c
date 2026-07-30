// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

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
#include "view/screens/screen_settings.h"

#include "model/battery.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <lvgl_input_device.h>
#include <stdio.h>
#include <string.h>

#include <drivers/display/hx8353e.h>

LOG_MODULE_REGISTER(app_ui, LOG_LEVEL_INF);

/* ---------- Menu item definitions (defined early; frame_ops needs them) -- */

/* dir is always ±1 (see menu_item_t); + ARRAY_SIZE keeps the modulo non-negative for dir=-1.
 * State for every item below lives in radio_settings.c (seeded from the codeplug where a
 * field mapping exists); the cycle/toggle/set functions here only compute the new value
 * and hand it to the matching settings_set_*(). Applying it to hardware and refreshing the
 * row's value_str buffer happens in on_settings_changed(), fired synchronously by the
 * setter via the settings module's subscriber notification. */

static char          s_squelch_val_buf[8] = "55";
static const uint8_t squelch_presets[] = { 30, 45, 55, 70, 85 };
static uint8_t        s_squelch_idx = 2; /* index of 55, matches radio_settings' default */

/** Cycles the squelch preset table. */
static void squelch_cycle(int8_t dir)
{
	s_squelch_idx = (uint8_t)(((int)s_squelch_idx + dir + ARRAY_SIZE(squelch_presets)) %
				  ARRAY_SIZE(squelch_presets));
	settings_set_squelch_level(squelch_presets[s_squelch_idx]);
}

static char s_bw_val_buf[8] = "25K";

/** Toggles 25K/12.5K bandwidth; dir is ignored (2-state toggle). */
static void bandwidth_cycle(int8_t dir)
{
	ARG_UNUSED(dir);
	settings_set_bandwidth_is_25k(!settings_get_bandwidth_is_25k());
}

/* VFO Up/Down step size, cycled from the RADIO settings menu. */
static const uint32_t    step_presets_hz[]    = { 2500, 5000, 6250, 12500, 25000 };
static const char *const step_preset_labels[] = { "2.5k", "5k", "6.25k", "12.5k", "25k" };
static uint8_t            s_step_idx = 1; /* 5 kHz default, matches radio_settings' default */
static char               s_step_val_buf[8] = "5k";

/** Cycles the VFO step-size preset table. */
static void step_cycle(int8_t dir)
{
	s_step_idx = (uint8_t)(((int)s_step_idx + dir + ARRAY_SIZE(step_presets_hz)) %
			       ARRAY_SIZE(step_presets_hz));
	settings_set_vfo_step_hz(step_presets_hz[s_step_idx]);
}

static void stub_item(int8_t dir)
{
	ARG_UNUSED(dir);
	LOG_INF("item: not yet implemented");
}

/* PWM backlight via display_set_brightness() (0-255, tracked as percent). Step sizes
 * are UI-domain granularity; the valid [min, max] range for each is owned by
 * radio_settings (settings_get_range()) since settings_set_*() clamps to it too --
 * see that module for why (fully dark by 6% on this hardware, hence the 10% floor). */
#define BRIGHTNESS_STEP_PCT  10

#define BACKLIGHT_OFF_STEP_PCT        10
#define BACKLIGHT_OFF_STEP_SMALL_PCT   1
#define BACKLIGHT_OFF_EDGE_BAND_PCT   10

static char s_backlight_off_val_buf[8] = "10%";
static char s_brightness_val_buf[8] = "100%";

/* Highest Off-level allowed: one dimming step below Brightness. */
static uint8_t backlight_off_ceiling(void)
{
	uint8_t brightness_pct = settings_get_brightness_pct();

	return (brightness_pct > BACKLIGHT_OFF_STEP_PCT)
		       ? (uint8_t)(brightness_pct - BACKLIGHT_OFF_STEP_PCT)
		       : 0;
}

/** Steps display brightness by dir, re-clamping backlight off-level below it.
 * settings_set_brightness_pct() clamps the new value into range itself. */
static void brightness_cycle(int8_t dir)
{
	int new_pct = (int)settings_get_brightness_pct() + (int)dir * BRIGHTNESS_STEP_PCT;

	settings_set_brightness_pct((uint8_t)new_pct);

	/* Re-clamp Off-level down if Brightness was just lowered past it. */
	uint8_t off_ceiling = backlight_off_ceiling();

	if (settings_get_backlight_off_pct() > off_ceiling) {
		settings_set_backlight_off_pct(off_ceiling);
	}
}

/** Steps the backlight off-level, using a smaller step near the range edges. */
static void backlight_off_level_cycle(int8_t dir)
{
	/* Shares Brightness's range: same PWM dead zone, see radio_settings.c. */
	int32_t brightness_min, brightness_max;

	settings_get_range(SETTINGS_KEY_BRIGHTNESS, &brightness_min, &brightness_max);

	uint8_t current = settings_get_backlight_off_pct();
	int new_pct;

	if (dir > 0 && current == 0) {
		new_pct = (int)brightness_min;
	} else {
		uint8_t step = (current < BACKLIGHT_OFF_EDGE_BAND_PCT ||
				current > (uint8_t)brightness_max - BACKLIGHT_OFF_EDGE_BAND_PCT)
				       ? BACKLIGHT_OFF_STEP_SMALL_PCT
				       : BACKLIGHT_OFF_STEP_PCT;
		new_pct = (int)current + (int)dir * step;

		if (new_pct < brightness_min) {
			new_pct = 0; /* skip the unreachable 1-9% zone */
		}
	}

	int ceiling = (int)backlight_off_ceiling();

	if (new_pct > ceiling) {
		new_pct = ceiling;
	}
	settings_set_backlight_off_pct((uint8_t)new_pct);
}

/* Idle seconds before dimming to the off-level (see ui_note_activity()); 0 = never.
 * Timeout only — no Auto/Squelch/Manual/Buttons mode picker. Max is owned by
 * radio_settings (settings_get_range()); this is just the UI's step granularity. */
#define SCREEN_TIMEOUT_STEP_S  5

static char s_screen_timeout_val_buf[8] = "Off";

/** Steps the screen-dim idle timeout; 0 means never dim. */
static void screen_dim_timeout_cycle(int8_t dir)
{
	int32_t min, max;

	settings_get_range(SETTINGS_KEY_SCREEN_TIMEOUT, &min, &max);

	int new_s = (int)settings_get_screen_timeout_s() + (int)dir * SCREEN_TIMEOUT_STEP_S;

	if (new_s > max) {
		new_s = (int)max;
	} else if (new_s < min) {
		new_s = (int)min;
	}
	settings_set_screen_timeout_s((uint8_t)new_s);
}

static char s_invert_val_buf[8] = "Off";

/** Sets screen invert on/off (dir>0 = on); set-semantics, not a toggle. */
static void screen_invert_set(int8_t dir)
{
	settings_set_screen_invert(dir > 0);
}

/* Gates app_overlay_volume_show()'s toast; volume itself still applies. */
static char s_visual_volume_val_buf[8] = "On";

static void visual_volume_toggle(int8_t dir)
{
	ARG_UNUSED(dir);
	settings_set_visual_volume_enabled(!settings_get_visual_volume_enabled());
}

/* Stored pref only; battery label is a fixed placeholder until ADC exists. */
static char s_battery_unit_val_buf[8] = "%";

static void battery_unit_toggle(int8_t dir)
{
	ARG_UNUSED(dir);
	settings_set_battery_unit_is_percent(!settings_get_battery_unit_is_percent());
}

/* Disabling forces both LEDs off; nothing lights them yet when enabled. */
static const struct gpio_dt_spec led_green_spec = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_red_spec   = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

static char s_leds_enabled_val_buf[8] = "On";

/** Sets LEDs enabled on/off (dir>0 = on). */
static void leds_enabled_set(int8_t dir)
{
	settings_set_leds_enabled(dir > 0);
}

/* Permanently blocked (no GPS/RTC/contacts/DMR); shown as N/A to keep the full menu shape visible. */
static void unavailable_item(int8_t dir)
{
	ARG_UNUSED(dir);
	LOG_INF("item: permanently unavailable on this hardware");
}

/** Applies a settings change to hardware (where applicable) and refreshes the row's
 * value_str buffer. Registered once via settings_subscribe() in app_init(); also fired
 * for every key by settings_apply_all() at boot. */
static void on_settings_changed(enum settings_key key)
{
	switch (key) {
	case SETTINGS_KEY_SQUELCH: {
		uint8_t level = settings_get_squelch_level();
		int rc = radio_state_set_squelch(level);

		if (rc < 0) {
			LOG_WRN("set_squelch(%u) failed: %d", level, rc);
		}
		snprintf(s_squelch_val_buf, sizeof(s_squelch_val_buf), "%u", level);
		break;
	}
	case SETTINGS_KEY_BANDWIDTH: {
		bool is_25k = settings_get_bandwidth_is_25k();
		int rc = radio_state_set_bandwidth(is_25k);

		if (rc < 0) {
			LOG_WRN("set_bandwidth failed: %d", rc);
		}
		snprintf(s_bw_val_buf, sizeof(s_bw_val_buf), "%s", is_25k ? "25K" : "12.5K");
		break;
	}
	case SETTINGS_KEY_VFO_STEP:
		/* s_step_idx already matches (only step_cycle() changes this key). */
		snprintf(s_step_val_buf, sizeof(s_step_val_buf), "%s", step_preset_labels[s_step_idx]);
		break;
	case SETTINGS_KEY_BRIGHTNESS: {
		uint8_t pct = settings_get_brightness_pct();
		const struct device *disp = DEVICE_DT_GET(DT_NODELABEL(hx8353e));
		int rc = display_set_brightness(
			disp, (uint8_t)DIV_ROUND_CLOSEST((uint32_t)pct * 255, 100));

		if (rc < 0) {
			LOG_WRN("display_set_brightness(%u%%) failed: %d", pct, rc);
		}
		snprintf(s_brightness_val_buf, sizeof(s_brightness_val_buf), "%u%%", pct);
		break;
	}
	case SETTINGS_KEY_BACKLIGHT_OFF:
		snprintf(s_backlight_off_val_buf, sizeof(s_backlight_off_val_buf), "%u%%",
			 settings_get_backlight_off_pct());
		break;
	case SETTINGS_KEY_SCREEN_TIMEOUT: {
		uint8_t s = settings_get_screen_timeout_s();

		if (s == 0) {
			snprintf(s_screen_timeout_val_buf, sizeof(s_screen_timeout_val_buf), "Off");
		} else {
			snprintf(s_screen_timeout_val_buf, sizeof(s_screen_timeout_val_buf), "%us", s);
		}
		break;
	}
	case SETTINGS_KEY_SCREEN_INVERT: {
		bool on = settings_get_screen_invert();
		const struct device *disp = DEVICE_DT_GET(DT_NODELABEL(hx8353e));
		int rc = hx8353e_set_inverted(disp, on);

		if (rc < 0) {
			LOG_WRN("hx8353e_set_inverted(%d) failed: %d", on, rc);
		}
		snprintf(s_invert_val_buf, sizeof(s_invert_val_buf), "%s", on ? "On" : "Off");
		break;
	}
	case SETTINGS_KEY_VISUAL_VOLUME:
		snprintf(s_visual_volume_val_buf, sizeof(s_visual_volume_val_buf), "%s",
			 settings_get_visual_volume_enabled() ? "On" : "Off");
		break;
	case SETTINGS_KEY_BATTERY_UNIT:
		snprintf(s_battery_unit_val_buf, sizeof(s_battery_unit_val_buf), "%s",
			 settings_get_battery_unit_is_percent() ? "%" : "V");
		break;
	case SETTINGS_KEY_LEDS_ENABLED: {
		bool on = settings_get_leds_enabled();

		if (!on) {
			int rc1 = gpio_pin_set_dt(&led_green_spec, 0);
			int rc2 = gpio_pin_set_dt(&led_red_spec, 0);

			if (rc1 < 0 || rc2 < 0) {
				LOG_WRN("LED force-off failed: %d/%d", rc1, rc2);
			}
		}
		snprintf(s_leds_enabled_val_buf, sizeof(s_leds_enabled_val_buf), "%s", on ? "On" : "Off");
		break;
	}
	default:
		break;
	}
}

static const menu_item_t radio_items[] = {
	{ "Squelch",   squelch_cycle,   s_squelch_val_buf },
	{ "Volume",    stub_item,       "7 %"             },
	{ "Bandwidth", bandwidth_cycle, s_bw_val_buf       },
	{ "Step",      step_cycle,      s_step_val_buf     },
	{ "CTCSS/DCS", stub_item,       "Off"              },
};

static const menu_item_t display_items[] = {
	{ "Brightness",          brightness_cycle,          s_brightness_val_buf     },
	{ "Screen dim timeout",  screen_dim_timeout_cycle,  s_screen_timeout_val_buf },
	{ "Backlight off-level", backlight_off_level_cycle, s_backlight_off_val_buf  },
	{ "Screen invert",       screen_invert_set,         s_invert_val_buf         },
	{ "Visual volume",       visual_volume_toggle,      s_visual_volume_val_buf  },
	{ "Battery unit",        battery_unit_toggle,       s_battery_unit_val_buf   },
	{ "All LEDs enabled",    leds_enabled_set,          s_leds_enabled_val_buf   },
	{ "Auto Night",          unavailable_item,          "N/A"                    },
	{ "Contact order",       unavailable_item,          "N/A"                    },
	{ "Split contact",       unavailable_item,          "N/A"                    },
	{ "Time in header",      unavailable_item,          "N/A"                    },
	{ "Extended infos",      unavailable_item,          "N/A"                    },
	{ "Timezone",            unavailable_item,          "N/A"                    },
	{ "UTC/Local time",      unavailable_item,          "N/A"                    },
	{ "Show distance",       unavailable_item,          "N/A"                    },
	{ "DMR last talker",     unavailable_item,          "N/A"                    },
};

static lv_obj_t *create_settings(lv_obj_t *parent)
{
	return screen_settings_create(parent,
				       radio_items, ARRAY_SIZE(radio_items),
				       display_items, ARRAY_SIZE(display_items));
}

/* ---------- Frame ops table -----------------------------------------------
 * SCREEN_BOOT has no entry -- one-shot transient, handled by app_init()/app_switch_screen(), not the static-frame model. */

typedef struct {
	lv_obj_t *(*create)(lv_obj_t *parent);
	void      (*update)(lv_obj_t *screen);
	void      (*handle_action)(lv_obj_t *screen, ui_action_t action);
} frame_ops_t;

/** OK opens Settings; Up/Down and the encoder both step the RX frequency. */
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

/** Attaches the shared input group to the keypad indev only while Settings or the quick-menu is on top. */
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

void app_post_action(ui_action_t action)
{
	/* Non-blocking: drop if queue full rather than block the caller. */
	(void)k_msgq_put(&ui_events, &action, K_NO_WAIT);
}

/* Depth-1, overwrite-latest: the pot reports a continuous position, so only the latest value before app_tick() matters. */
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
 * the LVGL thread regardless of frame, avoiding new cross-thread sync. */
static int64_t s_last_activity_ms;
static bool    s_backlight_dimmed;

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
	uint8_t pct = (uint8_t)DIV_ROUND_CLOSEST((uint32_t)(raw - VOL_AXIS_MIN) * 100,
						  VOL_AXIS_SPAN);
	int rc = radio_state_set_volume(pct);

	if (rc < 0) {
		LOG_WRN("set_volume(%u) failed: %d", pct, rc);
	}
}

/* 11-point piecewise-linear reverse-mapping LUTs (raw-span fraction -> display %);
 * selected at compile time by the AT1846S node's volume-taper DT property. */
static const uint8_t taper_lut_linear[11] = {
	0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
};

/* Standard "A" (audio/log) taper, ~14% output at 50% rotation on this board;
 * modeled as two linear segments rather than a smooth curve. */
static const uint8_t taper_lut_audio_a[11] = {
	0, 42, 55, 60, 66, 72, 77, 83, 89, 94, 100,
};

#if DT_ENUM_HAS_VALUE(DT_NODELABEL(at1846s), volume_taper, audio_a)
static const uint8_t *const s_volume_taper_lut = taper_lut_audio_a;
#else
static const uint8_t *const s_volume_taper_lut = taper_lut_linear;
#endif

/** Maps a raw pot reading to a display percent via the selected taper LUT, interpolating between checkpoints. */
static uint8_t volume_display_pct(uint16_t raw)
{
	if (raw >= VOL_AXIS_MAX) {
		return s_volume_taper_lut[10];
	}

	/* Scale by 10 before dividing by span so index and in-segment fraction
	 * come from one calculation, without requiring span to be a multiple of 10. */
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
		/* Priority: quick-menu overlay > tabview row level > leave the frame;
		 * each check only fires if the one above it doesn't apply. */
		if (overlay_quickmenu_is_active()) {
			overlay_quickmenu_hide();
		} else if (s_frame_top >= 0 && s_frame_stack[s_frame_top] == SCREEN_SETTINGS &&
			   screen_settings_in_rows_level()) {
			screen_settings_exit_rows_level();
		} else {
			app_pop_screen();
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

	/* Indevs start unattached; update_indev_group_attachment() attaches them as needed. */
	s_input_group = lv_group_create();
	lv_group_set_default(s_input_group);
	s_keypad_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_NODELABEL(lvgl_keypad)));

	/* Load settings (codeplug where mapped, else firmware defaults), register this
	 * module as a consumer, then push the result to hardware and the value_str
	 * buffers below before any frame is created -- so the first paint already shows
	 * the right values instead of waiting for the next 200 ms update tick. */
	settings_init();
	settings_subscribe(on_settings_changed);
	settings_apply_all();

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
	 * iteration after any frame switch or quick-menu show/dismiss. */
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
	lv_obj_remove_flag(s_frame_obj[s_frame_stack[s_frame_top]], LV_OBJ_FLAG_HIDDEN);

	LOG_DBG("pop to frame %d (depth %d)", s_frame_stack[s_frame_top], s_frame_top + 1);
}

/** One-time boot transition: destroys the boot splash and shows id as the base frame. */
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

	s_frame_top = 0;
	s_frame_stack[0] = id;
	lv_obj_remove_flag(s_frame_obj[id], LV_OBJ_FLAG_HIDDEN);

	LOG_DBG("switch to frame %d", id);
}

/** Shows the volume overlay only if the Visual volume preference is on. */
void app_overlay_volume_show(uint8_t pct)
{
	if (settings_get_visual_volume_enabled()) {
		overlay_volume_show(pct);
	}
}
