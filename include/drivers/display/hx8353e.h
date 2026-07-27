/*
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 * SPDX-License-Identifier: MIT
 *
 * HX8353E-specific display API not covered by Zephyr's generic
 * display_driver_api (zephyr/drivers/display.h has no inversion field).
 */

#ifndef DMRTASTIC_DRIVERS_DISPLAY_HX8353E_H_
#define DMRTASTIC_DRIVERS_DISPLAY_HX8353E_H_

#include <stdbool.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Toggle normal vs. inverted video; boot default is the bit-inversion DT property. */
int hx8353e_set_inverted(const struct device *dev, bool inverted);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_DRIVERS_DISPLAY_HX8353E_H_ */
