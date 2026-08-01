// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "channel_controller.h"
#include "controller/zone_controller.h"
#include "model/codeplug.h"
#include "model/radio_state.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define CP_CHANNEL_INDEX_MIN 1
#define CP_CHANNEL_INDEX_MAX 1024

static struct cp_channel s_channel;
static int s_index_1based; /* 0 = no in-use channel found */

/* Direct channel-number digit entry -- a plain accumulator, not a digit array like
 * fm_vfo_controller's fixed-8-digit frequency entry (see channel_controller.h). 0 means no
 * entry is in progress.
 */
static int s_entry_value;

/* Zone-scoped stepping (Milestone 4b) -- see channel_controller_set_zone_scoped()'s doc
 * comment. s_zone_pos is the position within zone_controller's current zone's channel list
 * that s_channel/s_index_1based currently reflect; -1 when that's untracked (not zone-scoped,
 * or the current channel came from a flat-table direct entry while zone-scoped).
 */
static bool s_zone_scoped;
static int s_zone_pos = -1;

/** Reads slot index_1based and reports whether it's a populated (in-use) channel. */
static bool load_channel(int index_1based, struct cp_channel *out)
{
	int rc = codeplug_get_channel(index_1based, out);

	if (rc < 0) {
		return false;
	}
	return codeplug_channel_is_in_use(out);
}

/** Programs rx frequency, bandwidth and CSS onto radio_state. TX power (ch->power) has no
 * radio_state hook to apply it to -- TX isn't wired to anything yet (Milestone 6 territory),
 * same gap as Milestone 2d's CSS work; it's only exposed via channel_controller_get_current()
 * for a future screen.
 */
static void apply_to_radio(const struct cp_channel *ch)
{
	int rc_freq = radio_state_set_frequency(ch->rxFreq);

	if (rc_freq < 0) {
		LOG_WRN("set_frequency(%u) failed: %d", ch->rxFreq, rc_freq);
	}

	int rc_bw = radio_state_set_bandwidth((ch->chFlag4 & CP_CHANNEL_FLAG4_BW_25K) != 0);

	if (rc_bw < 0) {
		LOG_WRN("set_bandwidth failed: %d", rc_bw);
	}

	/* TX first, RX second -- same AT1846S register-sharing ordering as
	 * settings_controller.c's on_settings_changed(SETTINGS_KEY_CSS): RX is what's actually
	 * in effect while not transmitting, so it must be the one applied last.
	 */
	struct cp_css tx_css = codeplug_decode_css(ch->txTone);
	struct cp_css rx_css = codeplug_decode_css(ch->rxTone);
	int rc_tx = radio_state_set_tx_css(&tx_css);
	int rc_rx = radio_state_set_rx_css(&rx_css);

	if (rc_tx < 0 || rc_rx < 0) {
		LOG_WRN("set_css failed: tx=%d rx=%d", rc_tx, rc_rx);
	}
}

/** Scans from start (exclusive) in the given direction, wrapping at the 1..1024 ends, for
 * the next in-use channel. Returns its index, or 0 if none exists anywhere in the table.
 */
static int find_next_in_use(int start, bool up, struct cp_channel *out)
{
	int idx = start;

	for (int i = 0; i < CP_CHANNEL_INDEX_MAX; i++) {
		idx = up ? (idx == CP_CHANNEL_INDEX_MAX ? CP_CHANNEL_INDEX_MIN : idx + 1)
			 : (idx == CP_CHANNEL_INDEX_MIN ? CP_CHANNEL_INDEX_MAX : idx - 1);

		if (load_channel(idx, out)) {
			return idx;
		}
	}
	return 0;
}

/** Loads the channel at position pos within zone_controller's current zone's channel list into
 * out, reporting whether it's in-use -- mirrors load_channel() but resolves the codeplug index
 * through zone_controller_get_channel_at() first.
 */
static bool load_zone_channel(int pos, struct cp_channel *out)
{
	uint16_t idx = zone_controller_get_channel_at(pos);

	if (idx == 0) {
		return false;
	}
	return load_channel(idx, out);
}

/** Scans zone_controller's current zone's channel-list positions from start (exclusive) in
 * the given direction, wrapping at the 0..count-1 ends, for the next in-use channel -- same
 * shape as find_next_in_use() but over the zone's list instead of the flat table. A start
 * outside 0..count-1 (including -1, used for initial resolution) is normalized first: -1 for
 * "up" (so the first candidate is position 0), count for "down" (so the first candidate is the
 * last position) -- this also recovers cleanly from a stale s_zone_pos left by a flat-table
 * direct entry (channel_controller_entry_commit() resets it to -1 regardless of direction).
 * Returns the position landed on, or -1 if none is in-use (including an empty zone).
 */
static int find_next_in_use_zone_pos(int start, bool up, struct cp_channel *out)
{
	int count = zone_controller_get_channel_count();

	if (count <= 0) {
		return -1;
	}

	int pos = (start >= 0 && start < count) ? start : (up ? -1 : count);

	for (int i = 0; i < count; i++) {
		pos = up ? (pos == count - 1 ? 0 : pos + 1) : (pos == 0 ? count - 1 : pos - 1);

		if (load_zone_channel(pos, out)) {
			return pos;
		}
	}
	return -1;
}

/* Deliberately doesn't call apply_to_radio() -- there's no mode arbitration yet between this
 * controller and fm_vfo_controller (that's Milestone 3c), and fm_vfo_controller_init() already
 * owns the radio for the FM VFO screen that's actually shown at boot. Caching here just makes
 * channel_controller_get_current()/_step() usable (e.g. from the console) without silently
 * retuning the radio out from under the active screen; channel_controller_step() is the first
 * point a caller has deliberately asked to switch to this controller's channel.
 */
void channel_controller_init(void)
{
	/* find_next_in_use() scans from (exclusive) start, so start one below the range to
	 * include index 1 on the very first "up" scan.
	 */
	int idx = find_next_in_use(CP_CHANNEL_INDEX_MIN - 1, true, &s_channel);

	if (idx == 0) {
		LOG_WRN("channel_controller_init: no in-use channel found in codeplug");
	}
	s_index_1based = idx;
}

void channel_controller_step(bool up)
{
	if (s_index_1based == 0) {
		return;
	}

	struct cp_channel candidate;

	if (s_zone_scoped) {
		int pos = find_next_in_use_zone_pos(s_zone_pos, up, &candidate);

		if (pos < 0) {
			LOG_WRN("channel_controller_step: no other in-use channel found in zone");
			return;
		}

		s_zone_pos = pos;
		s_index_1based = zone_controller_get_channel_at(pos);
		s_channel = candidate;
		apply_to_radio(&s_channel);
		return;
	}

	int idx = find_next_in_use(s_index_1based, up, &candidate);

	if (idx == 0) {
		LOG_WRN("channel_controller_step: no other in-use channel found");
		return;
	}

	s_index_1based = idx;
	s_channel = candidate;
	apply_to_radio(&s_channel);
}

bool channel_controller_get_current(struct cp_channel *out)
{
	if (s_index_1based == 0) {
		return false;
	}
	*out = s_channel;
	return true;
}

int channel_controller_get_current_index(void)
{
	return s_index_1based;
}

bool channel_controller_entry_active(void)
{
	return s_entry_value > 0;
}

int channel_controller_entry_value(void)
{
	return s_entry_value;
}

void channel_controller_entry_digit(int digit)
{
	if (digit < 0 || digit > 9) {
		return;
	}

	int candidate = s_entry_value * 10 + digit;

	if (candidate > CP_CHANNEL_INDEX_MAX) {
		/* Whole-entry reset on overflow, not a last-digit backout -- see
		 * channel_controller_entry_digit()'s doc comment.
		 */
		s_entry_value = 0;
		return;
	}
	s_entry_value = candidate;
}

void channel_controller_entry_commit(void)
{
	if (s_entry_value == 0) {
		return;
	}

	struct cp_channel candidate;

	if (load_channel(s_entry_value, &candidate)) {
		s_index_1based = s_entry_value;
		s_channel = candidate;
		/* Direct entry always targets the flat table regardless of s_zone_scoped -- see
		 * channel_controller_set_zone_scoped()'s doc comment -- so the committed channel
		 * may not be a member of the current zone's list; drop position tracking rather
		 * than leave it pointing at an unrelated entry.
		 */
		s_zone_pos = -1;
		apply_to_radio(&s_channel);
	} else {
		LOG_WRN("channel_controller_entry_commit: channel %d not in use", s_entry_value);
	}
	s_entry_value = 0;
}

void channel_controller_entry_cancel(void)
{
	s_entry_value = 0;
}

void channel_controller_set_zone_scoped(bool enabled)
{
	s_zone_scoped = enabled;

	if (!enabled) {
		s_zone_pos = -1;
		return;
	}

	struct cp_channel candidate;
	int pos = find_next_in_use_zone_pos(-1, true, &candidate);

	if (pos < 0) {
		LOG_WRN("channel_controller_set_zone_scoped: no in-use channel found in current "
			"zone");
		s_index_1based = 0;
		s_zone_pos = -1;
		return;
	}

	s_zone_pos = pos;
	s_index_1based = zone_controller_get_channel_at(pos);
	s_channel = candidate;
	apply_to_radio(&s_channel);
}

bool channel_controller_is_zone_scoped(void)
{
	return s_zone_scoped;
}
