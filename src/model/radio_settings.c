// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "radio_settings.h"
#include "codeplug.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(radio_settings, LOG_LEVEL_INF);

struct radio_settings {
	uint8_t  squelch_level;
	bool     bandwidth_is_25k;
	struct cp_css css;
	uint32_t vfo_step_hz;
	uint8_t  brightness_pct;
	uint8_t  backlight_off_pct;
	uint8_t  screen_timeout_s;
	bool     screen_invert;
	bool     visual_volume_enabled;
	bool     battery_unit_is_percent;
	bool     leds_enabled;
};

/* Matches the firmware defaults the UI used to hardcode; overridden by settings_init()
 * for the fields with a codeplug mapping when the codeplug is present and decodable. */
static struct radio_settings s_settings = {
	.squelch_level = 55,
	.bandwidth_is_25k = true,
	.css = { .type = CP_CSS_NONE, .value = 0, .inverted = false },
	.vfo_step_hz = 5000,
	.brightness_pct = 100,
	.backlight_off_pct = 10,
	.screen_timeout_s = 0,
	.screen_invert = false,
	.visual_volume_enabled = true,
	.battery_unit_is_percent = true,
	.leds_enabled = true,
};

struct settings_range {
	bool    defined;
	int32_t min;
	int32_t max;
};

/* Brightness/backlight-off share this floor: measured fully dark by 6% on this
 * hardware's PWM backlight, so 1-9% isn't a meaningfully distinct level. Screen
 * timeout's range matches the UI's Off..30s sweep. Keys not listed here (bools, and
 * fields whose valid values come from a fixed UI preset table) have no defined range. */
static const struct settings_range s_ranges[SETTINGS_KEY_COUNT] = {
	[SETTINGS_KEY_BRIGHTNESS]     = { .defined = true, .min = 10, .max = 100 },
	[SETTINGS_KEY_BACKLIGHT_OFF]  = { .defined = true, .min = 0,  .max = 100 },
	[SETTINGS_KEY_SCREEN_TIMEOUT] = { .defined = true, .min = 0,  .max = 30  },
};

bool settings_get_range(enum settings_key key, int32_t *min_out, int32_t *max_out)
{
	if (key >= SETTINGS_KEY_COUNT || !s_ranges[key].defined) {
		return false;
	}
	*min_out = s_ranges[key].min;
	*max_out = s_ranges[key].max;
	return true;
}

/* Clamps value into key's defined range, if any; passes it through unchanged otherwise. */
static int32_t clamp_to_range(enum settings_key key, int32_t value)
{
	int32_t min, max;

	if (settings_get_range(key, &min, &max)) {
		if (value < min) {
			value = min;
		} else if (value > max) {
			value = max;
		}
	}
	return value;
}

#define SETTINGS_MAX_SUBSCRIBERS 4

static settings_changed_cb_t s_subscribers[SETTINGS_MAX_SUBSCRIBERS];
static int                   s_subscriber_count;

void settings_subscribe(settings_changed_cb_t cb)
{
	if (s_subscriber_count >= SETTINGS_MAX_SUBSCRIBERS) {
		LOG_WRN("subscriber list full (%d), dropping registration", SETTINGS_MAX_SUBSCRIBERS);
		return;
	}
	s_subscribers[s_subscriber_count++] = cb;
}

static void notify(enum settings_key key)
{
	for (int i = 0; i < s_subscriber_count; i++) {
		s_subscribers[i](key);
	}
}

void settings_apply_all(void)
{
	for (int k = 0; k < SETTINGS_KEY_COUNT; k++) {
		notify((enum settings_key)k);
	}
}

/* Read-modify-write of the 3 fields this module owns in the on-flash nv-settings
 * block. Skips (logged) if the block isn't present/decodable -- same magic gate as
 * settings_init(). codeplug_write() is currently a no-op (-ENOTSUP, logged there),
 * so this doesn't touch flash yet, but the RMW logic is fully in place for when it
 * does. */
static void persist_nv(void)
{
	uint8_t buf[sizeof(struct cp_nv_settings)];
	uint32_t magic = 0;
	int rc = codeplug_get_nv_settings_raw(buf, sizeof(buf), &magic);

	if (rc < 0 || magic != CP_NV_SETTINGS_MAGIC_LATEST) {
		LOG_WRN("persist skipped: nv-settings unavailable/stale (rc=%d magic=0x%08x)",
			rc, magic);
		return;
	}

	struct cp_nv_settings *nv = (struct cp_nv_settings *)buf;

	nv->displayBacklightPercentage[0] = (int8_t)s_settings.brightness_pct;
	nv->displayBacklightPercentageOff = (int8_t)s_settings.backlight_off_pct;
	nv->backLightTimeout = s_settings.screen_timeout_s;

	(void)codeplug_set_nv_settings(nv);
}

void settings_init(void)
{
	uint8_t buf[sizeof(struct cp_nv_settings)];
	uint32_t magic = 0;
	int rc = codeplug_get_nv_settings_raw(buf, sizeof(buf), &magic);

	if (rc < 0 || magic != CP_NV_SETTINGS_MAGIC_LATEST) {
		LOG_WRN("codeplug nv-settings unavailable/stale (rc=%d magic=0x%08x); using firmware defaults",
			rc, magic);
		return;
	}

	struct cp_nv_settings *nv = (struct cp_nv_settings *)buf;

	s_settings.brightness_pct = (uint8_t)clamp_to_range(SETTINGS_KEY_BRIGHTNESS,
							     nv->displayBacklightPercentage[0]);
	s_settings.backlight_off_pct = (uint8_t)clamp_to_range(SETTINGS_KEY_BACKLIGHT_OFF,
								nv->displayBacklightPercentageOff);
	s_settings.screen_timeout_s = (uint8_t)clamp_to_range(SETTINGS_KEY_SCREEN_TIMEOUT,
							       nv->backLightTimeout);

	LOG_INF("loaded from codeplug: brightness=%u%% backlight_off=%u%% screen_timeout=%us",
		s_settings.brightness_pct, s_settings.backlight_off_pct, s_settings.screen_timeout_s);
}

uint8_t settings_get_squelch_level(void)
{
	return s_settings.squelch_level;
}

void settings_set_squelch_level(uint8_t level)
{
	s_settings.squelch_level = level;
	notify(SETTINGS_KEY_SQUELCH);
}

bool settings_get_bandwidth_is_25k(void)
{
	return s_settings.bandwidth_is_25k;
}

void settings_set_bandwidth_is_25k(bool is_25k)
{
	s_settings.bandwidth_is_25k = is_25k;
	notify(SETTINGS_KEY_BANDWIDTH);
}

struct cp_css settings_get_css(void)
{
	return s_settings.css;
}

void settings_set_css(struct cp_css css)
{
	s_settings.css = css;
	notify(SETTINGS_KEY_CSS);
}

uint32_t settings_get_vfo_step_hz(void)
{
	return s_settings.vfo_step_hz;
}

void settings_set_vfo_step_hz(uint32_t hz)
{
	s_settings.vfo_step_hz = hz;
	notify(SETTINGS_KEY_VFO_STEP);
}

uint8_t settings_get_brightness_pct(void)
{
	return s_settings.brightness_pct;
}

void settings_set_brightness_pct(uint8_t pct)
{
	s_settings.brightness_pct = (uint8_t)clamp_to_range(SETTINGS_KEY_BRIGHTNESS, pct);
	persist_nv();
	notify(SETTINGS_KEY_BRIGHTNESS);
}

uint8_t settings_get_backlight_off_pct(void)
{
	return s_settings.backlight_off_pct;
}

void settings_set_backlight_off_pct(uint8_t pct)
{
	s_settings.backlight_off_pct = (uint8_t)clamp_to_range(SETTINGS_KEY_BACKLIGHT_OFF, pct);
	persist_nv();
	notify(SETTINGS_KEY_BACKLIGHT_OFF);
}

uint8_t settings_get_screen_timeout_s(void)
{
	return s_settings.screen_timeout_s;
}

void settings_set_screen_timeout_s(uint8_t s)
{
	s_settings.screen_timeout_s = (uint8_t)clamp_to_range(SETTINGS_KEY_SCREEN_TIMEOUT, s);
	persist_nv();
	notify(SETTINGS_KEY_SCREEN_TIMEOUT);
}

bool settings_get_screen_invert(void)
{
	return s_settings.screen_invert;
}

void settings_set_screen_invert(bool on)
{
	s_settings.screen_invert = on;
	notify(SETTINGS_KEY_SCREEN_INVERT);
}

bool settings_get_visual_volume_enabled(void)
{
	return s_settings.visual_volume_enabled;
}

void settings_set_visual_volume_enabled(bool on)
{
	s_settings.visual_volume_enabled = on;
	notify(SETTINGS_KEY_VISUAL_VOLUME);
}

bool settings_get_battery_unit_is_percent(void)
{
	return s_settings.battery_unit_is_percent;
}

void settings_set_battery_unit_is_percent(bool is_percent)
{
	s_settings.battery_unit_is_percent = is_percent;
	notify(SETTINGS_KEY_BATTERY_UNIT);
}

bool settings_get_leds_enabled(void)
{
	return s_settings.leds_enabled;
}

void settings_set_leds_enabled(bool on)
{
	s_settings.leds_enabled = on;
	notify(SETTINGS_KEY_LEDS_ENABLED);
}
