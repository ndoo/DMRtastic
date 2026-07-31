// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "fm_vfo_controller.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* Debounce before programming the AT1846S: coalesces a burst of Up/Down
 * clicks into one retune instead of blocking I2C per detent. */
#define VFO_RETUNE_DEBOUNCE_MS 100

static uint32_t s_freq_hz;          /* shown/intended target frequency */
static uint8_t  s_rssi;             /* last-read signal byte */
static bool     s_squelch_open;
static bool     s_retune_pending;   /* s_freq_hz not yet programmed into hardware */
static int64_t  s_last_step_uptime; /* k_uptime_get() at the most recent step */

void fm_vfo_controller_tick(void)
{
	uint8_t signal = 0, noise = 0;

	radio_state_get_rssi(&signal, &noise);
	s_rssi = signal;
	s_squelch_open = (noise < settings_get_squelch_level());

	if (s_retune_pending) {
		/* Within the debounce window -- keep the optimistic shadow value. */
		if (k_uptime_get() - s_last_step_uptime >= VFO_RETUNE_DEBOUNCE_MS) {
			uint32_t target = s_freq_hz;
			int rc = radio_state_set_frequency(target);

			if (rc < 0) {
				uint32_t hw_freq_hz;

				LOG_WRN("set_frequency(%u) failed: %d", target, rc);
				/* Roll back to whatever's actually tuned. */
				if (radio_state_get_frequency(&hw_freq_hz) == 0) {
					s_freq_hz = hw_freq_hz;
				}
				s_retune_pending = false;
			} else if (s_freq_hz == target) {
				/* Shadow hasn't moved since target was captured -- now in sync. */
				s_retune_pending = false;
			}
			/* else: shadow moved again mid-write -- stay pending for the newer target. */
		}
	} else {
		uint32_t freq_hz = 0;

		if (radio_state_get_frequency(&freq_hz) == 0) {
			s_freq_hz = freq_hz;
		}
	}
}

void fm_vfo_controller_step(bool up)
{
	uint32_t step = settings_get_vfo_step_hz();

	/* No I2C here -- just move the shadow value and arm the debounced retune. */
	s_freq_hz = up ? s_freq_hz + step
		       : (s_freq_hz > step ? s_freq_hz - step : s_freq_hz);
	s_retune_pending = true;
	s_last_step_uptime = k_uptime_get();
}

uint32_t fm_vfo_controller_get_frequency_hz(void)
{
	return s_freq_hz;
}

uint8_t fm_vfo_controller_get_rssi(void)
{
	return s_rssi;
}

bool fm_vfo_controller_get_squelch_open(void)
{
	return s_squelch_open;
}
