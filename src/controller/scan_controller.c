// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "scan_controller.h"
#include "controller/channel_controller.h"
#include "controller/zone_controller.h"
#include "model/codeplug.h"
#include "model/radio_settings.h"
#include "model/radio_state.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

static enum scan_state s_state = SCAN_STATE_IDLE;
static bool s_up;
static bool s_prior_zone_scoped;

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

	s_up = up;
	s_state = SCAN_STATE_SCANNING;
}

void scan_controller_stop(void)
{
	if (s_state == SCAN_STATE_IDLE) {
		return;
	}

	s_state = SCAN_STATE_IDLE;
	if (!s_prior_zone_scoped) {
		channel_controller_set_zone_scoped(false);
	}
}

enum scan_state scan_controller_get_state(void)
{
	return s_state;
}

void scan_controller_tick(void)
{
	if (s_state == SCAN_STATE_IDLE) {
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
