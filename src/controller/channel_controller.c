// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "channel_controller.h"
#include "model/codeplug.h"
#include "model/radio_state.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#define CP_CHANNEL_INDEX_MIN 1
#define CP_CHANNEL_INDEX_MAX 1024

static struct cp_channel s_channel;
static int               s_index_1based; /* 0 = no in-use channel found */

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
 * for a future screen. */
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
	 * in effect while not transmitting, so it must be the one applied last. */
	struct cp_css tx_css = codeplug_decode_css(ch->txTone);
	struct cp_css rx_css = codeplug_decode_css(ch->rxTone);
	int rc_tx = radio_state_set_tx_css(&tx_css);
	int rc_rx = radio_state_set_rx_css(&rx_css);

	if (rc_tx < 0 || rc_rx < 0) {
		LOG_WRN("set_css failed: tx=%d rx=%d", rc_tx, rc_rx);
	}
}

/** Scans from start (exclusive) in the given direction, wrapping at the 1..1024 ends, for
 * the next in-use channel. Returns its index, or 0 if none exists anywhere in the table. */
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

/* Deliberately doesn't call apply_to_radio() -- there's no mode arbitration yet between this
 * controller and fm_vfo_controller (that's Milestone 3c), and fm_vfo_controller_init() already
 * owns the radio for the FM VFO screen that's actually shown at boot. Caching here just makes
 * channel_controller_get_current()/_step() usable (e.g. from the console) without silently
 * retuning the radio out from under the active screen; channel_controller_step() is the first
 * point a caller has deliberately asked to switch to this controller's channel. */
void channel_controller_init(void)
{
	/* find_next_in_use() scans from (exclusive) start, so start one below the range to
	 * include index 1 on the very first "up" scan. */
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
