/*
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO keyboard matrix that shares its sensed (row) pins with another
 * peripheral; scanned explicitly (see kbd_matrix_shared_bus_scan()) instead
 * of via the common driver's autonomous polling thread.
 */

#define DT_DRV_COMPAT gpio_kbd_matrix_shared_bus

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input/kbd_matrix_shared_bus.h>

LOG_MODULE_REGISTER(kbd_matrix_shared_bus, CONFIG_INPUT_LOG_LEVEL);

struct kbd_matrix_shared_bus_config {
	struct input_kbd_matrix_common_config common;
	const struct gpio_dt_spec *col_gpio; /* driven lines, dedicated pins */
	const struct gpio_dt_spec *row_gpio; /* sensed lines, shared pins */
};

struct kbd_matrix_shared_bus_data {
	struct input_kbd_matrix_common_data common;
};

INPUT_KBD_STRUCT_CHECK(struct kbd_matrix_shared_bus_config,
		       struct kbd_matrix_shared_bus_data);

/** Scan the matrix once and report debounced key events via the input subsystem. */
int kbd_matrix_shared_bus_scan(const struct device *dev)
{
	const struct kbd_matrix_shared_bus_config *cfg = dev->config;
	const struct input_kbd_matrix_common_config *common = &cfg->common;
	int ret;

	/*
	 * STM32 GPIO errata: force output-low before switching to input to
	 * avoid a glitch. NOPULL avoids sourcing current onto shared LCD lines.
	 */
	for (int r = 0; r < common->row_size; r++) {
		ret = gpio_pin_configure_dt(&cfg->row_gpio[r], GPIO_OUTPUT_LOW);
		if (ret != 0) {
			LOG_ERR("row %d force-low failed: %d", r, ret);
			return ret;
		}
	}

	k_busy_wait(10);

	for (int r = 0; r < common->row_size; r++) {
		ret = gpio_pin_configure_dt(&cfg->row_gpio[r], GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("row %d reconfigure-input failed: %d", r, ret);
			return ret;
		}
	}

	for (int c = 0; c < common->col_size; c++) {
		kbd_row_t mask = 0;

		ret = gpio_pin_set_dt(&cfg->col_gpio[c], 1);
		if (ret != 0) {
			LOG_ERR("col %d drive failed: %d", c, ret);
			return ret;
		}

		k_busy_wait(common->settle_time_us);

		for (int r = 0; r < common->row_size; r++) {
			if (gpio_pin_get_dt(&cfg->row_gpio[r])) {
				mask |= BIT(r);
			}
		}
		common->matrix_new_state[c] = mask;

		ret = gpio_pin_set_dt(&cfg->col_gpio[c], 0);
		if (ret != 0) {
			LOG_ERR("col %d release failed: %d", c, ret);
			return ret;
		}
	}

	for (int r = 0; r < common->row_size; r++) {
		ret = gpio_pin_configure_dt(&cfg->row_gpio[r], GPIO_OUTPUT_LOW);
		if (ret != 0) {
			LOG_ERR("row %d hand-back failed: %d", r, ret);
			return ret;
		}
	}

	input_kbd_matrix_update_state(dev);

	return 0;
}

/** Validate and configure the col GPIOs; row GPIOs are left to the peer's init. */
static int kbd_matrix_shared_bus_init(const struct device *dev)
{
	const struct kbd_matrix_shared_bus_config *cfg = dev->config;
	const struct input_kbd_matrix_common_config *common = &cfg->common;
	int ret;

	for (int c = 0; c < common->col_size; c++) {
		const struct gpio_dt_spec *gpio = &cfg->col_gpio[c];

		if (!gpio_is_ready_dt(gpio)) {
			LOG_ERR_DEVICE_NOT_READY(gpio->port);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(gpio, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("col %d configuration failed: %d", c, ret);
			return ret;
		}
	}

	for (int r = 0; r < common->row_size; r++) {
		if (!gpio_is_ready_dt(&cfg->row_gpio[r])) {
			LOG_ERR_DEVICE_NOT_READY(cfg->row_gpio[r].port);
			return -ENODEV;
		}
		/* Left as whatever the peer configured; scan() takes and
		 * hands back ownership each call. */
	}

	return 0;
}

#define KBD_MATRIX_SHARED_BUS_INIT(n)								\
	BUILD_ASSERT(DT_INST_PROP_LEN(n, row_gpios) <= INPUT_KBD_MATRIX_ROW_BITS,		\
		     "row-gpios exceeds kbd_row_t width");					\
												\
	INPUT_KBD_MATRIX_DT_INST_DEFINE_ROW_COL(						\
		n, DT_INST_PROP_LEN(n, row_gpios), DT_INST_PROP_LEN(n, col_gpios));		\
												\
	static const struct gpio_dt_spec kbd_matrix_shared_bus_row_gpio_##n[DT_INST_PROP_LEN(	\
			n, row_gpios)] = {							\
		DT_INST_FOREACH_PROP_ELEM_SEP(n, row_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))	\
	};											\
	static const struct gpio_dt_spec kbd_matrix_shared_bus_col_gpio_##n[DT_INST_PROP_LEN(	\
			n, col_gpios)] = {							\
		DT_INST_FOREACH_PROP_ELEM_SEP(n, col_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))	\
	};											\
												\
	static const struct kbd_matrix_shared_bus_config kbd_matrix_shared_bus_cfg_##n = {	\
		.common = INPUT_KBD_MATRIX_DT_INST_COMMON_CONFIG_INIT_ROW_COL(			\
			n, NULL,								\
			DT_INST_PROP_LEN(n, row_gpios), DT_INST_PROP_LEN(n, col_gpios)),	\
		.row_gpio = kbd_matrix_shared_bus_row_gpio_##n,				\
		.col_gpio = kbd_matrix_shared_bus_col_gpio_##n,				\
	};											\
												\
	static struct kbd_matrix_shared_bus_data kbd_matrix_shared_bus_data_##n;		\
												\
	DEVICE_DT_INST_DEFINE(n, kbd_matrix_shared_bus_init, NULL,				\
			      &kbd_matrix_shared_bus_data_##n, &kbd_matrix_shared_bus_cfg_##n,	\
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,				\
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(KBD_MATRIX_SHARED_BUS_INIT)
