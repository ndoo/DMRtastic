/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * Zone controller -- owns navigation over the codeplug's zone table (up to 68 physical
 * slots, in-use bitmap cached at init per codeplug_get_zone_inuse_bitmap()). No channel-
 * membership integration yet (Milestone 4b) and no "All Channels" virtual zone (also 4b/4c
 * territory -- it's a channel_controller integration concept, not a zone-table one); this
 * sub-milestone is navigation over populated zone slots only. Static module singleton, same
 * convention as channel_controller.c.
 */

#ifndef DMRTASTIC_ZONE_CONTROLLER_H_
#define DMRTASTIC_ZONE_CONTROLLER_H_

#include <stdbool.h>

#include "model/codeplug.h"

/** Reads codeplug_get_zone_inuse_bitmap() once and caches it (and the resulting in-use
 * count), then loads the first in-use zone slot. Call once at startup, before any other
 * zone_controller_*() call. Logs and leaves zone_controller_get_current() returning false
 * if the codeplug has no in-use zones.
 */
void zone_controller_init(void);

/** Moves to the next (up) or previous (down) in-use zone slot, wrapping at the ends of the
 * populated set, and caches its data. A no-op if no in-use zone exists at all.
 */
void zone_controller_step(bool up);

/** Copies the current zone (and its channels-per-zone, modern-80 vs legacy-16 per
 * codeplug_get_zone()) into *out/*channels_per_zone_out from the cache filled by init()/
 * step() -- no flash access here, same reasoning as channel_controller_get_current().
 * Returns false (leaving both untouched) if no in-use zone was found.
 */
bool zone_controller_get_current(struct cp_zone *out, int *channels_per_zone_out);

/** 0-based physical slot index of the current zone (matches codeplug_get_zone()'s
 * slot_index_0based parameter -- not "the nth populated zone"), -1 if none.
 */
int zone_controller_get_current_slot(void);

/** Number of in-use zone slots found at init, cached from the bitmap read there. */
int zone_controller_get_count(void);

#endif /* DMRTASTIC_ZONE_CONTROLLER_H_ */
