// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef DMRTASTIC_RADIO_STATE_H_
#define DMRTASTIC_RADIO_STATE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thin facade over the AT1846S transceiver driver -- Model layer: 1:1 driver
 * wrappers, no business logic. Centralizes DEVICE_DT_GET(at1846s) so
 * Controller/View code never touches <zephyr/device.h> or radio_transceiver.h
 * directly. */

/** Programs the RX frequency. */
int radio_state_set_frequency(uint32_t freq_hz);

/** Last RX frequency programmed via radio_state_set_frequency(); *freq_hz is 0 if never RX-tuned. */
int radio_state_get_frequency(uint32_t *freq_hz);

/** Raw signal/noise bytes (0-255) from the current RSSI reading. */
void radio_state_get_rssi(uint8_t *signal, uint8_t *noise);

/** Maps the current signal reading to a 0-4 bar count for the status bar. */
uint8_t radio_state_get_rssi_bars(void);

int radio_state_set_squelch(uint8_t level);
int radio_state_set_bandwidth(bool is_25k);
int radio_state_set_volume(uint8_t pct);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_RADIO_STATE_H_ */
