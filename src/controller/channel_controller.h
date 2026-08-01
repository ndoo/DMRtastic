/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * FM Channel controller -- owns the current-channel index into the codeplug's flat
 * 1024-slot channel table and applies its rx frequency/CSS/bandwidth to radio_state. Steps
 * across every in-use channel in index order by default; as of Milestone 4b, an optional
 * zone-scoped mode (channel_controller_set_zone_scoped()) restricts stepping to zone_
 * controller's current zone's channel list instead -- see that function's doc comment.
 * Static module singleton, same convention as fm_vfo_controller.c.
 */

#ifndef DMRTASTIC_CHANNEL_CONTROLLER_H_
#define DMRTASTIC_CHANNEL_CONTROLLER_H_

#include <stdbool.h>

#include "model/codeplug.h"

/** Finds the first in-use channel (index 1..1024) and applies it to radio_state.
 * Call once at startup, before any other channel_controller_*() call. Logs and leaves
 * channel_controller_get_current() returning false if the codeplug has no in-use channels.
 */
void channel_controller_init(void);

/** Moves to the next (up) or previous (down) in-use channel, wrapping at the ends of the
 * 1..1024 range, and applies it to radio_state. A no-op if no in-use channel exists at all.
 */
void channel_controller_step(bool up);

/** Copies the current channel into *out; returns false (leaving *out untouched) if no
 * in-use channel was found.
 */
bool channel_controller_get_current(struct cp_channel *out);

/** 1-based index of the current channel in the codeplug's flat table, 0 if none. */
int channel_controller_get_current_index(void);

/** True while a direct channel-number digit entry is in progress -- the view should render
 * channel_controller_entry_value() instead of the current channel while this is true.
 */
bool channel_controller_entry_active(void);

/** Accumulated value of the in-progress entry so far (0 if none is active). Rendered as a
 * plain decimal integer with no leading-zero padding, so unlike fm_vfo_controller's
 * fixed-width frequency entry, this has no per-digit position array -- a single accumulator
 * is enough.
 */
int channel_controller_entry_value(void);

/** Appends digit (0-9) to the in-progress entry, starting a new one if none is active. If the
 * accumulated value would exceed the flat 1..1024 table (no zone scoping yet -- Milestone 4),
 * the whole entry resets to empty rather than backing out just the last digit -- a channel
 * number has no fixed width to validate against until fully typed, unlike
 * fm_vfo_controller's fixed-8-digit frequency entry, so an out-of-range prefix is treated as
 * a mistyped entry rather than something to retry digit-by-digit. A leading 0 is a no-op,
 * same effect as fm_vfo_controller's leading-zero rejection.
 */
void channel_controller_entry_digit(int digit);

/** Commits the in-progress entry to the current channel if it names an in-use channel index,
 * applying it to radio_state same as channel_controller_step(); otherwise leaves the current
 * channel unchanged. Always clears the entry. A no-op if no entry is in progress. There's no
 * backspace for this entry -- commit or cancel are the only ways out.
 */
void channel_controller_entry_commit(void);

/** Cancels an in-progress entry without committing it. A no-op if none is in progress. */
void channel_controller_entry_cancel(void);

/** Enables or disables zone-scoped stepping (Milestone 4b). When enabled,
 * channel_controller_step() walks zone_controller's current zone's channel list (in list
 * order, wrapping at its ends) instead of the flat 1..1024 table; a channel named there is
 * still confirmed in-use via codeplug_channel_is_in_use() before landing on it, the same way
 * flat-table stepping already does, since a zone can reference a channel slot that's since
 * been cleared. Enabling immediately re-resolves the current channel to the nearest in-use
 * entry in the zone's list (scanning forward), applying it to radio_state; if the zone has no
 * in-use channel at all, channel_controller_get_current() starts returning false, same failure
 * mode as an empty flat table. Disabling leaves the channel that was current unchanged and
 * reverts stepping to the flat table from there.
 *
 * Direct-number entry (channel_controller_entry_*()) is unaffected by this flag either way --
 * it always targets the flat table. This matches how OpenGD77 behaves in its "All Channels"
 * virtual zone; a real (non-"All Channels") zone actually gives direct entry zone-relative-
 * position semantics there instead (confirmed by reading uiChannelMode.c while implementing
 * this), but this firmware has no "All Channels" virtual zone concept yet, so there's no
 * second entry semantics to switch between -- deferred to Milestone 4c alongside that
 * decision, see its Milestone Log entry.
 */
void channel_controller_set_zone_scoped(bool enabled);

/** True if zone-scoped stepping (see channel_controller_set_zone_scoped()) is currently
 * active.
 */
bool channel_controller_is_zone_scoped(void);

#endif /* DMRTASTIC_CHANNEL_CONTROLLER_H_ */
