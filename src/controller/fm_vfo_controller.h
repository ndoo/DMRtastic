// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * FM VFO controller -- owns the dual VFO A/B data model (two live
 * codeplug-shaped channel copies), the debounced retune/rollback state
 * machine, and the squelch-open derivation that screen_fm_vfo.c used to run
 * inline. Only one VFO screen instance exists, so this state is a static
 * module singleton rather than per-screen.
 */

#ifndef DMRTASTIC_FM_VFO_CONTROLLER_H_
#define DMRTASTIC_FM_VFO_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

/** Seeds VFO A/B from the codeplug and the active VFO from cp_nv_settings.currentVFONumber.
 * Call once at startup, before the first fm_vfo_controller_tick(). */
void fm_vfo_controller_init(void);

/** Polls RSSI/squelch and drives the debounced retune state machine. Call once per
 * update tick, before reading any of the getters below. */
void fm_vfo_controller_tick(void);

/** Steps the active VFO's target RX frequency by settings_get_vfo_step_hz() and arms the
 * debounced retune; the actual hardware write happens on a later fm_vfo_controller_tick(). */
void fm_vfo_controller_step(bool up);

/** Current/target RX frequency of the active VFO -- what the view should show, whether or
 * not it's been programmed into hardware yet. */
uint32_t fm_vfo_controller_get_frequency_hz(void);

/** Last-read signal byte (0-255). */
uint8_t fm_vfo_controller_get_rssi(void);

/** Whether the last-read noise byte is under the configured squelch level. */
bool fm_vfo_controller_get_squelch_open(void);

/** Active VFO index (0=A, 1=B). */
int fm_vfo_controller_get_current_vfo(void);

/** Switches the active VFO and arms a debounced retune to its stored frequency. RAM-only --
 * doesn't persist to the codeplug (codeplug_write() is a no-op until Milestone 10). */
void fm_vfo_controller_set_current_vfo(int vfo_ab);

#endif /* DMRTASTIC_FM_VFO_CONTROLLER_H_ */
