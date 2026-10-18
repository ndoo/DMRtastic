/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Signals the simulated transceiver can hear, keyed by frequency.
 */

#ifndef DMRTASTIC_SIM_SIM_RADIO_H_
#define DMRTASTIC_SIM_SIM_RADIO_H_

#include <stdint.h>

/** Add or update a signal; level is the RSSI signal byte (0-255). */
int sim_radio_set_signal(uint32_t freq_hz, uint8_t level);

void sim_radio_clear_signals(void);

#endif /* DMRTASTIC_SIM_SIM_RADIO_H_ */
