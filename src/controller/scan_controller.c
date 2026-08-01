// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "scan_controller.h"
#include "controller/channel_controller.h"
#include "controller/fm_vfo_controller.h"
#include "controller/zone_controller.h"
#include "model/codeplug.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* Only one of these can be active at a time -- scan_controller_start()/_start_vfo() are both
 * no-ops while s_state != SCAN_STATE_IDLE, which is exactly when s_target != SCAN_TARGET_NONE.
 */
enum scan_target {
	SCAN_TARGET_NONE = 0,
	SCAN_TARGET_CHANNEL,
	SCAN_TARGET_VFO,
};

/* +1 MHz -- OpenGD77's own scanInit() default VFO scan span when vfoScanLow/High are unset. */
#define VFO_SCAN_DEFAULT_SPAN_HZ 1000000UL

static enum scan_state s_state = SCAN_STATE_IDLE;
static enum scan_target s_target = SCAN_TARGET_NONE;
static bool s_up;
static bool s_prior_zone_scoped;    /* SCAN_TARGET_CHANNEL only */
static uint32_t s_vfo_scan_low_hz;  /* SCAN_TARGET_VFO only */
static uint32_t s_vfo_scan_high_hz; /* SCAN_TARGET_VFO only */

/** True if ch is flagged to skip during a zone scan -- see CP_CHANNEL_FLAG4_ZONE_SKIP's doc
 * comment.
 */
static bool is_zone_skip(const struct cp_channel *ch)
{
	return (ch->chFlag4 & CP_CHANNEL_FLAG4_ZONE_SKIP) != 0;
}

/** Steps channel_controller forward/back until it lands on a channel without the zone-skip
 * flag, bounded by the zone's channel count so an all-skipped zone can't loop forever -- in
 * that case it's left on whatever channel_controller_step() last landed on.
 */
static void step_to_next_eligible(bool up)
{
	int count = zone_controller_get_channel_count();

	if (count <= 0) {
		return;
	}

	struct cp_channel ch;

	for (int i = 0; i < count; i++) {
		channel_controller_step(up);

		if (!channel_controller_get_current(&ch) || !is_zone_skip(&ch)) {
			return;
		}
	}
	LOG_WRN("scan_controller: every channel in the current zone is skip-flagged");
}

void scan_controller_start(bool up)
{
	if (s_state != SCAN_STATE_IDLE) {
		return;
	}

	s_prior_zone_scoped = channel_controller_is_zone_scoped();
	if (!s_prior_zone_scoped) {
		channel_controller_set_zone_scoped(true);
	}

	s_target = SCAN_TARGET_CHANNEL;
	s_up = up;
	s_state = SCAN_STATE_SCANNING;
}

/** True if hz falls within either hardware band -- same membership test
 * fm_vfo_controller.c's own freq_in_band() applies, duplicated here rather than exposed since
 * it's a one-line check over fm_vfo_controller_get_band_limits()'s output.
 */
static bool freq_in_either_band(uint32_t hz, uint32_t vhf_min, uint32_t vhf_max, uint32_t uhf_min,
				uint32_t uhf_max)
{
	return (hz >= vhf_min && hz <= vhf_max) || (hz >= uhf_min && hz <= uhf_max);
}

/** OpenGD77's scanInit() fallback when vfoScanLow[]/vfoScanHigh[] read as unset (0): sweep from
 * the current RX frequency up to +1 MHz, or if that would land in the VHF/UHF dead-air gap or
 * past the top of the current band, up to the near edge of the other band instead.
 */
static void compute_default_vfo_bounds(uint32_t *low_hz, uint32_t *high_hz)
{
	uint32_t vhf_min, vhf_max, uhf_min, uhf_max;

	fm_vfo_controller_get_band_limits(&vhf_min, &vhf_max, &uhf_min, &uhf_max);

	uint32_t rx = fm_vfo_controller_get_frequency_hz();
	uint32_t up_edge = rx + VFO_SCAN_DEFAULT_SPAN_HZ;

	if (!freq_in_either_band(up_edge, vhf_min, vhf_max, uhf_min, uhf_max)) {
		up_edge = (rx <= vhf_max) ? uhf_min : uhf_max;
	}

	*low_hz = MIN(rx, up_edge);
	*high_hz = MAX(rx, up_edge);
}

/** vfoScanLow[]/vfoScanHigh[] are read straight off cp_nv_settings, which -- unlike
 * codeplug.c's BCD-decoded cp_channel rxFreq/txFreq -- this firmware doesn't decode per-field
 * (codeplug_get_nv_settings_raw() is a raw passthrough). OpenGD77 itself compares these fields
 * directly against its own post-BCD-decode in-RAM channel frequency with no extra scaling
 * (uiVFOMode.c), which is in units of 10 Hz; this firmware's own decode reaches whole Hz via
 * one further *10 (codeplug.c's cp_bcd_to_uint32() callers), so the same *10 is needed here to
 * land in the Hz fm_vfo_controller's getters/setters use. Returns false (bounds untouched) if
 * the nv-settings block is unreadable/stale or either bound is the unset (0) sentinel.
 */
static bool read_vfo_scan_bounds(int vfo_ab, uint32_t *low_hz, uint32_t *high_hz)
{
	uint8_t buf[sizeof(struct cp_nv_settings)];
	uint32_t magic = 0;
	int rc = codeplug_get_nv_settings_raw(buf, sizeof(buf), &magic);

	if (rc != 0 || magic != CP_NV_SETTINGS_MAGIC_LATEST) {
		return false;
	}

	struct cp_nv_settings *nv = (struct cp_nv_settings *)buf;

	if (nv->vfoScanLow[vfo_ab] == 0 || nv->vfoScanHigh[vfo_ab] == 0) {
		return false;
	}

	*low_hz = nv->vfoScanLow[vfo_ab] * 10UL;
	*high_hz = nv->vfoScanHigh[vfo_ab] * 10UL;
	return true;
}

void scan_controller_start_vfo(bool up)
{
	if (s_state != SCAN_STATE_IDLE) {
		return;
	}

	int vfo_ab = fm_vfo_controller_get_current_vfo();

	if (!read_vfo_scan_bounds(vfo_ab, &s_vfo_scan_low_hz, &s_vfo_scan_high_hz)) {
		compute_default_vfo_bounds(&s_vfo_scan_low_hz, &s_vfo_scan_high_hz);
	}

	s_target = SCAN_TARGET_VFO;
	s_up = up;
	s_state = SCAN_STATE_SCANNING;
}

void scan_controller_stop(void)
{
	if (s_state == SCAN_STATE_IDLE) {
		return;
	}

	s_state = SCAN_STATE_IDLE;
	if (s_target == SCAN_TARGET_CHANNEL && !s_prior_zone_scoped) {
		channel_controller_set_zone_scoped(false);
	}
	s_target = SCAN_TARGET_NONE;
}

void scan_controller_reverse_direction(void)
{
	if (s_state == SCAN_STATE_IDLE) {
		return;
	}

	s_up = !s_up;
}

enum scan_state scan_controller_get_state(void)
{
	return s_state;
}

void scan_controller_tick(void)
{
	if (s_target != SCAN_TARGET_CHANNEL) {
		return;
	}

	struct cp_channel ch;

	if (!channel_controller_get_current(&ch)) {
		/* Nothing in the zone to scan -- stay idle-in-place rather than spin. */
		return;
	}

	/* Defensive, not just belt-and-braces: channel_controller_set_zone_scoped() (called
	 * from scan_controller_start()) resolves to the nearest in-use zone entry without
	 * checking the skip flag, so the channel scanning starts on could itself be
	 * skip-flagged -- never pause here without checking squelch at all.
	 */
	if (is_zone_skip(&ch)) {
		s_state = SCAN_STATE_SCANNING;
		step_to_next_eligible(s_up);
		return;
	}

	uint8_t signal, noise;

	radio_state_get_rssi(&signal, &noise);
	bool squelch_open = noise < settings_get_squelch_level();

	if (squelch_open) {
		s_state = SCAN_STATE_PAUSED;
		return;
	}

	s_state = SCAN_STATE_SCANNING;
	step_to_next_eligible(s_up);
}

void scan_controller_tick_vfo(void)
{
	if (s_target != SCAN_TARGET_VFO) {
		return;
	}

	uint8_t signal, noise;

	radio_state_get_rssi(&signal, &noise);
	bool squelch_open = noise < settings_get_squelch_level();

	if (squelch_open) {
		s_state = SCAN_STATE_PAUSED;
		return;
	}

	s_state = SCAN_STATE_SCANNING;

	uint32_t freq = fm_vfo_controller_get_frequency_hz();
	uint32_t step = settings_get_vfo_step_hz();
	uint32_t next;

	if (s_up) {
		next = freq + step;
		if (next > s_vfo_scan_high_hz) {
			next = s_vfo_scan_low_hz;
		}
	} else {
		next = (freq > step) ? freq - step : 0;
		if (next < s_vfo_scan_low_hz) {
			next = s_vfo_scan_high_hz;
		}
	}

	fm_vfo_controller_set_frequency_hz(next);
}
