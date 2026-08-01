// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "settings_controller.h"
#include "view/screens/screen_settings.h"

#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>

#include <drivers/display/hx8353e.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* ---------- Menu item definitions ---------------------------------------
 *
 * dir is always ±1 (see menu_item_t); + ARRAY_SIZE keeps the modulo non-negative for dir=-1.
 * State for every item below lives in radio_settings.c (seeded from the codeplug where a
 * field mapping exists); the cycle/toggle/set functions here only compute the new value
 * and hand it to the matching settings_set_*(). Applying it to hardware and refreshing the
 * row's value_str buffer happens in on_settings_changed(), fired synchronously by the
 * setter via the settings module's subscriber notification.
 */

static char s_squelch_val_buf[8] = "55";
static const uint8_t squelch_presets[] = {30, 45, 55, 70, 85};
static uint8_t s_squelch_idx = 2; /* index of 55, matches radio_settings' default */

void settings_controller_cycle_squelch(int8_t dir)
{
	s_squelch_idx = (uint8_t)(((int)s_squelch_idx + dir + ARRAY_SIZE(squelch_presets)) %
				  ARRAY_SIZE(squelch_presets));
	settings_set_squelch_level(squelch_presets[s_squelch_idx]);
}

static char s_bw_val_buf[8] = "25K";

void settings_controller_cycle_bandwidth(int8_t dir)
{
	ARG_UNUSED(dir);
	settings_set_bandwidth_is_25k(!settings_get_bandwidth_is_25k());
}

/* Standard 50-tone CTCSS set (tenths of Hz). DCS isn't offered here -- the AT1846S
 * driver's set_tx_dcs/set_rx_dcs are still -ENOTSUP stubs (see radio_state.c), so
 * cycling to a DCS code would silently do nothing; add it to this table once driver
 * support lands.
 */
static const uint16_t css_ctcss_tenths_hz[] = {
	670,  693,  719,  744,  770,  797,  825,  854,  885,  915,  948,  974,  1000,
	1035, 1072, 1109, 1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567,
	1598, 1622, 1655, 1679, 1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966,
	1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
};

#define CSS_CYCLE_COUNT (ARRAY_SIZE(css_ctcss_tenths_hz) + 1) /* +1 for "Off" at index 0 */

static uint8_t s_css_idx; /* 0 = Off, matches radio_settings' default */
/* Sized for the full uint16_t tenths-Hz range ("6553.5Hz" + NUL), not just this table's
 * actual max (254.1Hz) -- css.value's static type is wider than what
 * settings_controller_cycle_css() ever produces, and the compiler warns on the type's
 * range, not the reachable one.
 */
static char s_css_val_buf[10] = "Off";

void settings_controller_cycle_css(int8_t dir)
{
	s_css_idx = (uint8_t)(((int)s_css_idx + dir + CSS_CYCLE_COUNT) % CSS_CYCLE_COUNT);

	struct cp_css css = {.type = CP_CSS_NONE, .value = 0, .inverted = false};

	if (s_css_idx > 0) {
		css.type = CP_CSS_CTCSS;
		css.value = css_ctcss_tenths_hz[s_css_idx - 1];
	}
	settings_set_css(css);
}

/* VFO Up/Down step size, cycled from the RADIO settings menu. */
static const uint32_t step_presets_hz[] = {2500, 5000, 6250, 12500, 25000};
static const char *const step_preset_labels[] = {"2.5k", "5k", "6.25k", "12.5k", "25k"};
static uint8_t s_step_idx = 1; /* 5 kHz default, matches radio_settings' default */
static char s_step_val_buf[8] = "5k";

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
 * see that module for why (fully dark by 6% on this hardware, hence the 10% floor).
 */
#define BRIGHTNESS_STEP_PCT 10

#define BACKLIGHT_OFF_STEP_PCT       10
#define BACKLIGHT_OFF_STEP_SMALL_PCT 1
#define BACKLIGHT_OFF_EDGE_BAND_PCT  10

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
 * settings_set_brightness_pct() clamps the new value into range itself.
 */
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

/* Idle seconds before dimming to the off-level (see app.c's ui_note_activity()); 0 = never.
 * Timeout only — no Auto/Squelch/Manual/Buttons mode picker. Max is owned by
 * radio_settings (settings_get_range()); this is just the UI's step granularity.
 */
#define SCREEN_TIMEOUT_STEP_S 5

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
static const struct gpio_dt_spec led_red_spec = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

static char s_leds_enabled_val_buf[8] = "On";

/** Sets LEDs enabled on/off (dir>0 = on). */
static void leds_enabled_set(int8_t dir)
{
	settings_set_leds_enabled(dir > 0);
}

/* Permanently blocked (no GPS/RTC/contacts/DMR); shown as N/A to keep the full menu shape visible.
 */
static void unavailable_item(int8_t dir)
{
	ARG_UNUSED(dir);
	LOG_INF("item: permanently unavailable on this hardware");
}

/** Applies a settings change to hardware (where applicable) and refreshes the row's
 * value_str buffer. Registered once via settings_subscribe() in settings_controller_init();
 * also fired for every key by settings_apply_all() at boot.
 */
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
	case SETTINGS_KEY_CSS: {
		struct cp_css css = settings_get_css();
		/* TX first, RX second -- deliberate order, not arbitrary. TX and RX CTCSS
		 * share the same physical registers on the AT1846S (CTCSS1 for TX tone,
		 * CTCSS2 for RX tone/detect -- see at1846s_api_set_tx_ctcss()/
		 * _set_rx_ctcss()), and each one's "on" path explicitly zeroes the
		 * other's register as part of isolating its own. There's no PTT-time
		 * apply/restore sequence yet (TX isn't wired to a PTT path -- Milestone 6
		 * territory), so this single settings-change callback has to apply both
		 * back to back itself. Applying RX last ensures CTCSS2 (RX detect) is the
		 * one left standing, since RX is what's actually in effect while not
		 * transmitting; applying TX first and letting RX overwrite whatever TX
		 * just zeroed is harmless today. Getting this backwards silently disables
		 * RX tone detection: CTCSS2 would read as configured immediately after
		 * set_rx_css() but then get zeroed a moment later by set_tx_css()'s own
		 * cleanup step.
		 */
		int rc2 = radio_state_set_tx_css(&css);
		int rc1 = radio_state_set_rx_css(&css);

		if (rc1 < 0 || rc2 < 0) {
			LOG_WRN("set_css failed: rx=%d tx=%d", rc1, rc2);
		}
		if (css.type == CP_CSS_CTCSS) {
			snprintf(s_css_val_buf, sizeof(s_css_val_buf), "%u.%uHz", css.value / 10,
				 css.value % 10);
		} else {
			snprintf(s_css_val_buf, sizeof(s_css_val_buf), "Off");
		}
		break;
	}
	case SETTINGS_KEY_VFO_STEP:
		/* s_step_idx already matches (only step_cycle() changes this key). */
		snprintf(s_step_val_buf, sizeof(s_step_val_buf), "%s",
			 step_preset_labels[s_step_idx]);
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
			snprintf(s_screen_timeout_val_buf, sizeof(s_screen_timeout_val_buf), "%us",
				 s);
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
		snprintf(s_leds_enabled_val_buf, sizeof(s_leds_enabled_val_buf), "%s",
			 on ? "On" : "Off");
		break;
	}
	default:
		break;
	}
}

static const menu_item_t radio_items[] = {
	{"Squelch", settings_controller_cycle_squelch, s_squelch_val_buf},
	{"Volume", stub_item, "7 %"},
	{"Bandwidth", settings_controller_cycle_bandwidth, s_bw_val_buf},
	{"Step", step_cycle, s_step_val_buf},
	{"CTCSS/DCS", settings_controller_cycle_css, s_css_val_buf},
};

static const menu_item_t display_items[] = {
	{"Brightness", brightness_cycle, s_brightness_val_buf},
	{"Screen dim timeout", screen_dim_timeout_cycle, s_screen_timeout_val_buf},
	{"Backlight off-level", backlight_off_level_cycle, s_backlight_off_val_buf},
	{"Screen invert", screen_invert_set, s_invert_val_buf},
	{"Visual volume", visual_volume_toggle, s_visual_volume_val_buf},
	{"Battery unit", battery_unit_toggle, s_battery_unit_val_buf},
	{"All LEDs enabled", leds_enabled_set, s_leds_enabled_val_buf},
	{"Auto Night", unavailable_item, "N/A"},
	{"Contact order", unavailable_item, "N/A"},
	{"Split contact", unavailable_item, "N/A"},
	{"Time in header", unavailable_item, "N/A"},
	{"Extended infos", unavailable_item, "N/A"},
	{"Timezone", unavailable_item, "N/A"},
	{"UTC/Local time", unavailable_item, "N/A"},
	{"Show distance", unavailable_item, "N/A"},
	{"DMR last talker", unavailable_item, "N/A"},
};

void settings_controller_init(void)
{
	/* Load settings (codeplug where mapped, else firmware defaults), register this
	 * module as a consumer, then push the result to hardware and the value_str
	 * buffers above before any screen is created -- so the first paint already shows
	 * the right values instead of waiting for the next 200 ms update tick.
	 */
	settings_init();
	settings_subscribe(on_settings_changed);
	settings_apply_all();
}

lv_obj_t *settings_controller_create_screen(lv_obj_t *parent)
{
	return screen_settings_create(parent, radio_items, ARRAY_SIZE(radio_items), display_items,
				      ARRAY_SIZE(display_items));
}

void settings_controller_get_radio_items(const menu_item_t **items, uint8_t *count)
{
	*items = radio_items;
	*count = ARRAY_SIZE(radio_items);
}

void settings_controller_get_display_items(const menu_item_t **items, uint8_t *count)
{
	*items = display_items;
	*count = ARRAY_SIZE(display_items);
}
