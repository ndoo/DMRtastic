/* SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Public API for the "gpio-kbd-matrix-shared-bus" input driver
 * (drivers/kbd_matrix_shared_bus/): no interrupt or polling thread, so it
 * can safely share sensed (row) pins with another peripheral between scans.
 */

#ifndef DMRTASTIC_DRIVERS_INPUT_KBD_MATRIX_SHARED_BUS_H_
#define DMRTASTIC_DRIVERS_INPUT_KBD_MATRIX_SHARED_BUS_H_

#include <zephyr/device.h>

/**
 * Scan the matrix once and report debounced key events via the input subsystem.
 * Must be called from the same thread that drives the peer sharing the row pins — no locking.
 */
int kbd_matrix_shared_bus_scan(const struct device *dev);

#endif /* DMRTASTIC_DRIVERS_INPUT_KBD_MATRIX_SHARED_BUS_H_ */
