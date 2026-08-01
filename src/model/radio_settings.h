/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_RADIO_SETTINGS_H_
#define DMRTASTIC_RADIO_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>

#include "codeplug.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared runtime settings state -- seeded from the codeplug (where a field mapping
 * exists) or firmware defaults, mutated by the UI, and persisted back to the codeplug
 * (currently a no-op -- see codeplug_write()). Anything that needs to react to a
 * setting change subscribes via settings_subscribe(); anything that just needs the
 * current value calls the matching settings_get_*().
 */

enum settings_key {
	SETTINGS_KEY_SQUELCH,
	SETTINGS_KEY_BANDWIDTH,
	SETTINGS_KEY_CSS,
	SETTINGS_KEY_VFO_STEP,
	SETTINGS_KEY_BRIGHTNESS,
	SETTINGS_KEY_BACKLIGHT_OFF,
	SETTINGS_KEY_SCREEN_TIMEOUT,
	SETTINGS_KEY_SCREEN_INVERT,
	SETTINGS_KEY_VISUAL_VOLUME,
	SETTINGS_KEY_BATTERY_UNIT,
	SETTINGS_KEY_LEDS_ENABLED,
	SETTINGS_KEY_COUNT,
};

typedef void (*settings_changed_cb_t)(enum settings_key key);

/* Loads state from the codeplug (nv-settings block, magic-gated) where a field mapping
 * exists, falling back to firmware defaults otherwise. Call once at boot, before
 * subscribing/applying. Does not touch hardware or notify subscribers.
 */
void settings_init(void);

/* Inclusive [min, max] value range for keys that have one (numeric fields such as
 * Brightness); returns false for keys with no defined range (bools, and fields whose
 * valid values come from a fixed UI preset table instead). settings_set_*() clamps to
 * this range internally, so it's the single source of truth for any caller -- consult
 * it instead of hardcoding bounds, e.g. for a cycle function's own step math.
 */
bool settings_get_range(enum settings_key key, int32_t *min_out, int32_t *max_out);

/* Registers cb to be called on every settings_set_*(). Fixed-size subscriber list --
 * logs and drops the registration if full.
 */
void settings_subscribe(settings_changed_cb_t cb);

/* Fires every subscriber for every key once -- used at boot to push the
 * loaded/defaulted state out to hardware after subscribers have registered.
 */
void settings_apply_all(void);

uint8_t settings_get_squelch_level(void);
void settings_set_squelch_level(uint8_t level);

bool settings_get_bandwidth_is_25k(void);
void settings_set_bandwidth_is_25k(bool is_25k);

/* Shared tone/code applied to both TX and RX -- this radio's UI (quick menu and Settings
 * Radio tab) exposes a single "CTCSS/DCS" control, not independent TX/RX tones, so there's
 * only one value to track here. CP_CSS_NONE (the default) means off.
 */
struct cp_css settings_get_css(void);
void settings_set_css(struct cp_css css);

uint32_t settings_get_vfo_step_hz(void);
void settings_set_vfo_step_hz(uint32_t hz);

/* Backed by the codeplug nv-settings block (displayBacklightPercentage[0]). */
uint8_t settings_get_brightness_pct(void);
void settings_set_brightness_pct(uint8_t pct);

/* Backed by the codeplug nv-settings block (displayBacklightPercentageOff). */
uint8_t settings_get_backlight_off_pct(void);
void settings_set_backlight_off_pct(uint8_t pct);

/* Backed by the codeplug nv-settings block (backLightTimeout). */
uint8_t settings_get_screen_timeout_s(void);
void settings_set_screen_timeout_s(uint8_t s);

bool settings_get_screen_invert(void);
void settings_set_screen_invert(bool on);

bool settings_get_visual_volume_enabled(void);
void settings_set_visual_volume_enabled(bool on);

bool settings_get_battery_unit_is_percent(void);
void settings_set_battery_unit_is_percent(bool is_percent);

bool settings_get_leds_enabled(void);
void settings_set_leds_enabled(bool on);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_RADIO_SETTINGS_H_ */
