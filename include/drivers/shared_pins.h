// SPDX-License-Identifier: Apache-2.0
#ifndef DMRTASTIC_SHARED_PINS_H
#define DMRTASTIC_SHARED_PINS_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>

int shared_pins_acquire(const struct device *dev, k_timeout_t timeout);
void shared_pins_release(const struct device *dev);

#endif
