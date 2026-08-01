/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

/*
 * FM channel scan controller -- Milestone 5a. Design decision: wraps channel_controller
 * rather than teaching channel_controller (or fm_vfo_controller) a scanning mode of its own.
 * channel_controller already owns index/apply-to-radio/zone-scoped-stepping (Milestone 4b) and
 * has no scan-specific concepts (dwell timing, pause state, skip flags); scan_controller only
 * needs to drive its existing public step()/get_current() API on a timer and read squelch state
 * from radio_state directly (same derivation fm_vfo_controller_tick() uses), so a thin
 * controller-on-top-of-controller keeps those concerns layered instead of tangled into
 * channel_controller itself. This mirrors channel_controller's own relationship to
 * zone_controller -- controller-consuming-controller is already this codebase's pattern, not a
 * new one introduced here.
 *
 * Tick ownership: scan_controller_tick() is called from screen_fm_channel.c's update(), the
 * same way fm_vfo_controller_tick() is only called from screen_fm_vfo.c's update() -- radio
 * ownership in this firmware is implicitly "whichever screen's update() is currently running",
 * and ticking scan_controller unconditionally (e.g. from app.c's screen-agnostic 200 ms timer)
 * would let it keep retuning the radio to zone channels even while the FM VFO screen is
 * topmost and its own tick is separately trying to hold a VFO frequency -- a contention this
 * firmware has no arbitration for yet. Scanning only progresses while the FM Channel screen is
 * visible; starting it from elsewhere (e.g. the console) arms the state but it stays parked
 * until that screen's update() next runs. scan_controller_tick() is still safe to call
 * directly (e.g. from a console command) for verification without the screen visible -- it
 * only touches channel_controller/radio_state, not anything screen-specific.
 */

#ifndef DMRTASTIC_SCAN_CONTROLLER_H_
#define DMRTASTIC_SCAN_CONTROLLER_H_

#include <stdbool.h>

enum scan_state {
	SCAN_STATE_IDLE = 0,
	SCAN_STATE_SCANNING,
	SCAN_STATE_PAUSED, /* squelch open on the current channel -- holding here */
};

/** Starts FM channel scan in the given step direction, walking channel_controller's
 * zone-scoped channel list (Milestone 4b) -- enables zone-scoped mode if it wasn't already on,
 * restored to whatever it was on scan_controller_stop(). A no-op if already active.
 */
void scan_controller_start(bool up);

/** Stops scanning, leaving the current channel as-is, and restores whatever zone-scoped mode
 * channel_controller was in before scan_controller_start(). A no-op if not active.
 */
void scan_controller_stop(void);

/** Current scan state -- SCAN_STATE_IDLE when not scanning. */
enum scan_state scan_controller_get_state(void);

/** Polls squelch on the current channel and drives the scan state machine: steps to the next
 * non-skip in-use channel in the zone's list while the squelch is closed, or holds
 * (SCAN_STATE_PAUSED) on the current channel while it's open. A no-op while idle. See this
 * header's top comment for why callers must not assume this runs on a screen-agnostic timer.
 */
void scan_controller_tick(void);

#endif /* DMRTASTIC_SCAN_CONTROLLER_H_ */
