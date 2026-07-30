// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef DMRTASTIC_BATTERY_H_
#define DMRTASTIC_BATTERY_H_

#include <stdint.h>

/** One-shot init: configures the ADC channel and takes an initial reading. */
int battery_init(void);

/** Self-throttled; safe to call every tick, only samples the ADC at its own internal rate. */
void battery_poll(void);

/** Latest measured pack voltage in millivolts, 0 if no reading yet. */
uint16_t battery_get_millivolts(void);

/** Latest battery percentage (0-100) from the OCV curve, 0 if no reading yet. */
uint8_t battery_get_percent(void);

#endif /* DMRTASTIC_BATTERY_H_ */
