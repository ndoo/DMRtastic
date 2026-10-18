// SPDX-License-Identifier: Apache-2.0
#define DT_DRV_COMPAT ht_radio_shared_pins

#include <drivers/shared_pins.h>

struct shared_pins_data {
	struct k_mutex lock;
};

int shared_pins_acquire(const struct device *dev, k_timeout_t timeout)
{
	struct shared_pins_data *data = dev->data;

	return k_mutex_lock(&data->lock, timeout);
}

void shared_pins_release(const struct device *dev)
{
	struct shared_pins_data *data = dev->data;

	k_mutex_unlock(&data->lock);
}

static int shared_pins_init(const struct device *dev)
{
	struct shared_pins_data *data = dev->data;

	return k_mutex_init(&data->lock);
}

#define SHARED_PINS_DEFINE(n)                                                                      \
	static struct shared_pins_data shared_pins_data_##n;                                       \
	DEVICE_DT_INST_DEFINE(n, shared_pins_init, NULL, &shared_pins_data_##n, NULL, POST_KERNEL, \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

DT_INST_FOREACH_STATUS_OKAY(SHARED_PINS_DEFINE)
