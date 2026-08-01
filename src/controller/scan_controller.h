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
 *
 * VFO frequency scan (Milestone 5b) is a second, independent mode added on top of the above --
 * wraps fm_vfo_controller the same way channel scan wraps channel_controller, tracked via an
 * internal target (channel/VFO/none) rather than a second copy of the state machine, since only
 * one can be active at a time (scan_controller_start()/_start_vfo() are both no-ops while the
 * other is already running). scan_controller_tick() and the new scan_controller_tick_vfo() are
 * both safe to call unconditionally from either screen's update() -- each is a no-op unless its
 * own target is the one currently active -- which preserves channel scan's existing "only
 * progresses while its own screen is visible" invariant now that screen_fm_vfo.c also ticks
 * scan_controller every update(), rather than a mismatched screen silently driving the wrong
 * controller. Sweep bounds come from cp_nv_settings' per-VFO vfoScanLow[]/vfoScanHigh[] when
 * set, else default to [current RX frequency, +1 MHz] band-limit-aware, matching OpenGD77's own
 * uiVFOMode.c scanInit() -- see scan_controller_start_vfo()'s doc comment below for the full
 * citation. Stepping wraps at the bounds (steps past the high edge restart at the low edge and
 * vice-versa) rather than reversing direction or clamping.
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

/** Starts VFO frequency scan (Milestone 5b) on the currently active VFO
 * (fm_vfo_controller_get_current_vfo()), stepping by settings_get_vfo_step_hz() in the given
 * direction and wrapping at cp_nv_settings' per-VFO vfoScanLow[]/vfoScanHigh[] -- confirmed
 * against OpenGD77's uiVFOMode.c (scanInit()/scanning()), not guessed: a step that would cross
 * the high bound while scanning up (or the low bound while scanning down) wraps to the
 * opposite bound instead of reversing or clamping. If either bound reads as its on-flash unset
 * sentinel (0 -- OpenGD77's own default), falls back for this session only to [current RX
 * frequency, current RX frequency + 1 MHz], jumping the +1 MHz edge to the other band's near
 * edge if that would otherwise land in the VHF/UHF dead-air gap or past the hardware ceiling --
 * same fallback OpenGD77's scanInit() computes, not a new default invented here. Not
 * persisted -- codeplug_write() is a no-op until Milestone 10, same RAM-only gap as every other
 * setting edit in this firmware; a scan-boundary-editing UI is deliberately out of scope for
 * 5b (see the Milestone Log for why this fallback makes that unnecessary for a first pass).
 * A no-op if a scan (either mode) is already active.
 */
void scan_controller_start_vfo(bool up);

/** Stops whichever scan is currently active (channel or VFO), leaving the current
 * channel/frequency as-is, and restores whatever zone-scoped mode channel_controller was in
 * before scan_controller_start() if that was the active mode. A no-op if not active.
 */
void scan_controller_stop(void);

/** Current scan state -- SCAN_STATE_IDLE when not scanning, regardless of which mode was last
 * active.
 */
enum scan_state scan_controller_get_state(void);

/** Polls squelch on the current channel and drives the FM channel scan state machine: steps to
 * the next non-skip in-use channel in the zone's list while the squelch is closed, or holds
 * (SCAN_STATE_PAUSED) on the current channel while it's open. A no-op unless channel scan is
 * the currently active mode (idle, or VFO scan active instead, are both no-ops here). See this
 * header's top comment for why callers must not assume this runs on a screen-agnostic timer.
 */
void scan_controller_tick(void);

/** Polls squelch on the active VFO and drives the VFO frequency scan state machine (Milestone
 * 5b): steps/wraps within scan_controller_start_vfo()'s bounds while squelch is closed, or
 * holds (SCAN_STATE_PAUSED) on the current frequency while it's open. A no-op unless VFO scan
 * is the currently active mode. Call from screen_fm_vfo.c's update() -- same tick-ownership
 * reasoning as scan_controller_tick() above.
 */
void scan_controller_tick_vfo(void);

#endif /* DMRTASTIC_SCAN_CONTROLLER_H_ */
