// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "fm_vfo_controller.h"
#include "model/codeplug.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* Debounce before programming the AT1846S: coalesces a burst of Up/Down
 * clicks (or a VFO switch) into one retune instead of blocking I2C per detent. */
#define VFO_RETUNE_DEBOUNCE_MS 100

/* Two live codeplug-shaped VFO copies -- index matches cp_nv_settings.currentVFONumber's
 * 0=A/1=B meaning. Edits are RAM-only until codeplug_write() is implemented (Milestone 10). */
static struct cp_channel s_vfo[2];
static uint8_t  s_current_vfo;      /* 0=A, 1=B */
static uint8_t  s_rssi;             /* last-read signal byte */
static bool     s_squelch_open;
static bool     s_retune_pending;   /* s_vfo[s_current_vfo].rxFreq not yet programmed into hardware */
static int64_t  s_last_step_uptime; /* k_uptime_get() at the most recent step/switch */

/** Seeds both VFO copies from the codeplug and arms an initial retune so hardware ends up
 * tuned to the active VFO before the next idle tick would otherwise pull a stale reading
 * back from it. Call once at startup, before the first fm_vfo_controller_tick(). */
void fm_vfo_controller_init(void)
{
	for (int i = 0; i < 2; i++) {
		int rc = codeplug_get_vfo_channel(i, &s_vfo[i]);

		if (rc < 0) {
			LOG_WRN("codeplug_get_vfo_channel(%d) failed: %d; VFO %c defaults to 0 Hz",
				i, rc, 'A' + i);
		}
	}

	uint8_t buf[sizeof(struct cp_nv_settings)];
	uint32_t magic = 0;
	int rc = codeplug_get_nv_settings_raw(buf, sizeof(buf), &magic);

	if (rc == 0 && magic == CP_NV_SETTINGS_MAGIC_LATEST) {
		struct cp_nv_settings *nv = (struct cp_nv_settings *)buf;

		s_current_vfo = (nv->currentVFONumber <= 1) ? nv->currentVFONumber : 0;
	} else {
		LOG_WRN("codeplug nv-settings unavailable/stale (rc=%d magic=0x%08x); defaulting to VFO A",
			rc, magic);
		s_current_vfo = 0;
	}

	s_retune_pending = true;
	s_last_step_uptime = k_uptime_get();
}

void fm_vfo_controller_tick(void)
{
	uint8_t signal = 0, noise = 0;

	radio_state_get_rssi(&signal, &noise);
	s_rssi = signal;
	s_squelch_open = (noise < settings_get_squelch_level());

	if (s_retune_pending) {
		/* Within the debounce window -- keep the optimistic shadow value. */
		if (k_uptime_get() - s_last_step_uptime >= VFO_RETUNE_DEBOUNCE_MS) {
			uint32_t target = s_vfo[s_current_vfo].rxFreq;
			int rc = radio_state_set_frequency(target);

			if (rc < 0) {
				uint32_t hw_freq_hz;

				LOG_WRN("set_frequency(%u) failed: %d", target, rc);
				/* Roll back to whatever's actually tuned. */
				if (radio_state_get_frequency(&hw_freq_hz) == 0) {
					s_vfo[s_current_vfo].rxFreq = hw_freq_hz;
				}
				s_retune_pending = false;
			} else if (s_vfo[s_current_vfo].rxFreq == target) {
				/* Shadow hasn't moved since target was captured -- now in sync. */
				s_retune_pending = false;
			}
			/* else: shadow moved again mid-write -- stay pending for the newer target. */
		}
	} else {
		uint32_t freq_hz = 0;

		if (radio_state_get_frequency(&freq_hz) == 0) {
			s_vfo[s_current_vfo].rxFreq = freq_hz;
		}
	}
}

void fm_vfo_controller_step(bool up)
{
	uint32_t step = settings_get_vfo_step_hz();
	uint32_t *freq = &s_vfo[s_current_vfo].rxFreq;

	/* No I2C here -- just move the shadow value and arm the debounced retune. */
	*freq = up ? *freq + step : (*freq > step ? *freq - step : *freq);
	s_retune_pending = true;
	s_last_step_uptime = k_uptime_get();
}

uint32_t fm_vfo_controller_get_frequency_hz(void)
{
	return s_vfo[s_current_vfo].rxFreq;
}

uint8_t fm_vfo_controller_get_rssi(void)
{
	return s_rssi;
}

bool fm_vfo_controller_get_squelch_open(void)
{
	return s_squelch_open;
}

int fm_vfo_controller_get_current_vfo(void)
{
	return s_current_vfo;
}

void fm_vfo_controller_set_current_vfo(int vfo_ab)
{
	if (vfo_ab != 0 && vfo_ab != 1) {
		LOG_WRN("set_current_vfo(%d): out of range, ignored", vfo_ab);
		return;
	}

	s_current_vfo = (uint8_t)vfo_ab;
	s_retune_pending = true;
	s_last_step_uptime = k_uptime_get();
}
