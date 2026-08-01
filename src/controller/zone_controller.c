// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "zone_controller.h"
#include "model/codeplug.h"

#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

/* Matches codeplug_get_zone()'s own slot_index_0based bound check -- the bitmap itself is
 * 32 bytes (256 bits) wide, but only the first 68 slots back real zone-table storage.
 */
#define ZONE_SLOT_MAX 68

static uint8_t s_bitmap[32];
static int s_count;
static struct cp_zone s_zone;
static int s_channels_per_zone;
static int s_channel_count; /* populated entries in s_zone.channels[], see count_channels() */
static int s_current_slot;  /* -1 = no in-use zone found */

static bool slot_is_in_use(int slot)
{
	return ((s_bitmap[slot / 8] >> (slot % 8)) & 0x01) != 0;
}

/** Counts populated entries in zone->channels[0..channels_per_zone) -- see
 * zone_controller_get_channel_count()'s doc comment for the stopping rule.
 */
static int count_channels(const struct cp_zone *zone, int channels_per_zone)
{
	for (int i = 0; i < channels_per_zone; i++) {
		if (zone->channels[i] == 0) {
			return i;
		}
	}
	return channels_per_zone;
}

/** Reads slot into out/channels_per_zone_out; returns false on a read error. Doesn't check
 * in-use -- unlike channel/contact slots, zone in-use is tracked by the separate bitmap
 * (s_bitmap), not a name[0]==0xFF marker, so the caller checks slot_is_in_use() itself.
 */
static bool load_zone(int slot, struct cp_zone *out, int *channels_per_zone_out)
{
	return codeplug_get_zone(slot, out, channels_per_zone_out) >= 0;
}

/** Scans from start (exclusive) in the given direction, wrapping at the 0..ZONE_SLOT_MAX-1
 * ends, for the next in-use slot. Returns its index, or -1 if none exists anywhere.
 */
static int find_next_in_use(int start, bool up)
{
	int idx = start;

	for (int i = 0; i < ZONE_SLOT_MAX; i++) {
		idx = up ? (idx == ZONE_SLOT_MAX - 1 ? 0 : idx + 1)
			 : (idx == 0 ? ZONE_SLOT_MAX - 1 : idx - 1);

		if (slot_is_in_use(idx)) {
			return idx;
		}
	}
	return -1;
}

void zone_controller_init(void)
{
	int rc = codeplug_get_zone_inuse_bitmap(s_bitmap);

	if (rc < 0) {
		LOG_WRN("zone_controller_init: bitmap read failed: %d", rc);
		memset(s_bitmap, 0, sizeof(s_bitmap));
	}

	s_count = 0;
	for (int i = 0; i < ZONE_SLOT_MAX; i++) {
		if (slot_is_in_use(i)) {
			s_count++;
		}
	}

	/* find_next_in_use() scans from (exclusive) start, so start one below the range to
	 * include slot 0 on the very first "up" scan.
	 */
	s_current_slot = find_next_in_use(-1, true);
	if (s_current_slot == -1) {
		LOG_WRN("zone_controller_init: no in-use zone found in codeplug");
		return;
	}

	if (!load_zone(s_current_slot, &s_zone, &s_channels_per_zone)) {
		LOG_WRN("zone_controller_init: read of slot %d failed", s_current_slot);
		s_current_slot = -1;
		return;
	}
	s_channel_count = count_channels(&s_zone, s_channels_per_zone);
}

void zone_controller_step(bool up)
{
	if (s_current_slot == -1) {
		return;
	}

	int idx = find_next_in_use(s_current_slot, up);

	if (idx == -1) {
		LOG_WRN("zone_controller_step: no other in-use zone found");
		return;
	}

	struct cp_zone candidate;
	int channels_per_zone;

	if (!load_zone(idx, &candidate, &channels_per_zone)) {
		LOG_WRN("zone_controller_step: read of slot %d failed", idx);
		return;
	}

	s_current_slot = idx;
	s_zone = candidate;
	s_channels_per_zone = channels_per_zone;
	s_channel_count = count_channels(&s_zone, s_channels_per_zone);
}

bool zone_controller_get_current(struct cp_zone *out, int *channels_per_zone_out)
{
	if (s_current_slot == -1) {
		return false;
	}
	*out = s_zone;
	*channels_per_zone_out = s_channels_per_zone;
	return true;
}

int zone_controller_get_current_slot(void)
{
	return s_current_slot;
}

int zone_controller_get_count(void)
{
	return s_count;
}

int zone_controller_get_channel_count(void)
{
	return s_current_slot == -1 ? 0 : s_channel_count;
}

uint16_t zone_controller_get_channel_at(int pos)
{
	if (s_current_slot == -1 || pos < 0 || pos >= s_channel_count) {
		return 0;
	}
	return s_zone.channels[pos];
}
