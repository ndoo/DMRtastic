// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/*
 * Owns the only call to display_set_brightness() in the tree, so every brightness change --
 * automatic idle-timeout dim/undim (app.c) or a Settings-menu change (settings_controller.c) --
 * fades instead of snapping. Callers never see raw PWM values, durations, or easing curves; they
 * just say what to set and how urgently (see backlight.h).
 *
 * Steps the fade on its own k_timer rather than an LVGL lv_anim: lv_anim only advances when
 * lv_timer_handler() runs, which is display.c's LVGL thread at its own ~20 Hz cadence
 * (k_msleep(50))
 * -- fine for the small per-step deltas a Settings-menu row produces, but not enough steps to look
 * smooth over a large delta like the full undim jump. A dedicated k_timer decouples the fade's
 * update rate from the UI thread entirely. Its callback runs in ISR context (Zephyr timer expiry),
 * which is fine here: PWM3 is a plain STM32 hardware timer channel (mduv390plus.dts), so
 * pwm_set_pulse_dt() is a direct register write, not a blocking I2C/SPI transaction -- same
 * reasoning as watchdog.c's own K_TIMER_DEFINE callback driving a GPIO directly.
 */

#include "backlight.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>

LOG_MODULE_REGISTER(backlight, LOG_LEVEL_INF);

/* Automatic idle-timeout dim-out only, matching meshtastic-device-ui's
 * ScreenSleepController::timeoutDimDuration -- deliberately slow so the screen doesn't visibly
 * "give up" the moment it goes idle. Every other fade (undim-on-activity, Settings-menu changes,
 * the initial fade-up from backlight_init()'s 0) uses the fast duration instead.
 */
#define BACKLIGHT_FADE_SLOW_MS 10000
#define BACKLIGHT_FADE_FAST_MS 250

/* Fade step rate -- independent of the LVGL thread's own ~20 Hz repaint cadence, see file header.
 */
#define BACKLIGHT_FADE_TICK_MS 10

#define HALF_PI 1.57079633f

static const struct device *s_disp;
static int32_t s_current_raw;

static int32_t s_fade_start_raw;
static int32_t s_fade_target_raw;
static int64_t s_fade_start_ms;
static uint32_t s_fade_duration_ms;

/** Ganssle cos_32 (~3.2 accurate decimal digits), valid for x in [0, HALF_PI]. No libm dependency.
 * https://www.ganssle.com/approx.htm
 */
static float cos_q1(float x)
{
	const float c1 = 0.99940307f;
	const float c2 = -0.49558072f;
	const float c3 = 0.03679168f;
	float x2 = x * x;

	return c1 + x2 * (c2 + x2 * c3);
}

static void fade_timer_cb(struct k_timer *timer);

K_TIMER_DEFINE(s_fade_timer, fade_timer_cb, NULL);

/* sin(x) isn't needed as a separate function: sin(t*HALF_PI) == cos_q1(HALF_PI - t*HALF_PI) ==
 * cos_q1((1-t)*HALF_PI), so one polynomial covers both the dim and brighten branches below.
 */
static void fade_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	float t = (float)(k_uptime_get() - s_fade_start_ms) / (float)s_fade_duration_ms;
	bool done = t >= 1.0f;

	if (done) {
		t = 1.0f;
	}

	float eased = (s_fade_target_raw < s_fade_start_raw)
			      ? 1.0f - cos_q1(t * HALF_PI)    /* dim: ease-in fall */
			      : cos_q1((1.0f - t) * HALF_PI); /* brighten: ease-out rise */
	int32_t raw = s_fade_start_raw + (int32_t)((s_fade_target_raw - s_fade_start_raw) * eased);
	int rc = display_set_brightness(s_disp, (uint8_t)raw);

	if (rc < 0) {
		LOG_WRN("display_set_brightness(%d) failed: %d", raw, rc);
	}
	s_current_raw = raw;

	if (done) {
		k_timer_stop(&s_fade_timer);
	}
}

void backlight_init(void)
{
	s_disp = DEVICE_DT_GET(DT_NODELABEL(hx8353e));

	int rc = display_set_brightness(s_disp, 0);

	if (rc < 0) {
		LOG_WRN("display_set_brightness(0) failed: %d", rc);
	}
	s_current_raw = 0;
}

void backlight_set_pct(uint8_t pct, bool slow)
{
	int32_t target_raw = (int32_t)DIV_ROUND_CLOSEST((uint32_t)pct * 255, 100);

	k_timer_stop(&s_fade_timer); /* cancel any in-flight fade; redirect from wherever it is */

	if (target_raw == s_current_raw) {
		return;
	}

	s_fade_start_raw = s_current_raw;
	s_fade_target_raw = target_raw;
	s_fade_start_ms = k_uptime_get();
	s_fade_duration_ms = slow ? BACKLIGHT_FADE_SLOW_MS : BACKLIGHT_FADE_FAST_MS;

	k_timer_start(&s_fade_timer, K_MSEC(BACKLIGHT_FADE_TICK_MS),
		      K_MSEC(BACKLIGHT_FADE_TICK_MS));
}
