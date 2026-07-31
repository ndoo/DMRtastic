// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * FM VFO controller -- owns the debounced retune/rollback state machine and
 * squelch-open derivation that screen_fm_vfo.c used to run inline. Only one
 * VFO screen instance exists, so this state is a static module singleton
 * rather than per-screen.
 */

#ifndef DMRTASTIC_FM_VFO_CONTROLLER_H_
#define DMRTASTIC_FM_VFO_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

/** Polls RSSI/squelch and drives the debounced retune state machine. Call once per
 * update tick, before reading any of the getters below. */
void fm_vfo_controller_tick(void);

/** Steps the target RX frequency by settings_get_vfo_step_hz() and arms the debounced
 * retune; the actual hardware write happens on a later fm_vfo_controller_tick(). */
void fm_vfo_controller_step(bool up);

/** Current/target RX frequency -- what the view should show, whether or not it's been
 * programmed into hardware yet. */
uint32_t fm_vfo_controller_get_frequency_hz(void);

/** Last-read signal byte (0-255). */
uint8_t fm_vfo_controller_get_rssi(void);

/** Whether the last-read noise byte is under the configured squelch level. */
bool fm_vfo_controller_get_squelch_open(void);

#endif /* DMRTASTIC_FM_VFO_CONTROLLER_H_ */
