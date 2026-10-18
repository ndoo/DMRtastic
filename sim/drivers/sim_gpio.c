// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#define DT_DRV_COMPAT dmrtastic_sim_gpio

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/kernel.h>

#include <sim/sim_gpio.h>

#define SIM_GPIO_MASK 0xFFFFU

struct sim_gpio_config {
	struct gpio_driver_config common;
};

struct sim_gpio_data {
	struct gpio_driver_data common;
	struct k_spinlock lock;
	uint32_t out;
	uint32_t out_en;
	uint32_t pull_up;
	uint32_t force_mask;
	uint32_t force_val;
	uint32_t last_in;
	uint32_t int_rise;
	uint32_t int_fall;
	uint32_t int_high;
	uint32_t int_low;
	sys_slist_t callbacks;
	sim_gpio_notify_fn notify;
	void *notify_ctx;
	sim_gpio_peer_fn peer;
	void *peer_ctx;
};

static uint32_t sim_gpio_resolve(struct sim_gpio_data *d)
{
	uint32_t out_en, out, pull_up, force_mask, force_val;
	uint32_t in_pins;
	uint32_t level;
	sim_gpio_peer_fn peer;
	void *peer_ctx;

	K_SPINLOCK(&d->lock) {
		out_en = d->out_en;
		out = d->out;
		pull_up = d->pull_up;
		force_mask = d->force_mask;
		force_val = d->force_val;
		peer = d->peer;
		peer_ctx = d->peer_ctx;
	}

	in_pins = ~out_en & SIM_GPIO_MASK;
	level = pull_up & in_pins;
	if (peer != NULL) {
		uint32_t pmask = 0;
		uint32_t pval = peer(peer_ctx, &pmask);

		level = (level & ~pmask) | (pval & pmask);
	}
	level = (level & ~force_mask) | (force_val & force_mask);

	return ((out & out_en) | (level & in_pins)) & SIM_GPIO_MASK;
}

static void sim_gpio_fire_interrupts(const struct device *port)
{
	struct sim_gpio_data *d = port->data;
	uint32_t now = sim_gpio_resolve(d);
	uint32_t fire = 0;

	K_SPINLOCK(&d->lock) {
		uint32_t changed = now ^ d->last_in;
		uint32_t rose = changed & now;
		uint32_t fell = changed & ~now;

		fire = (rose & d->int_rise) | (fell & d->int_fall) |
		       (changed & now & d->int_high) | (changed & ~now & d->int_low);
		d->last_in = now;
	}

	if (fire != 0) {
		gpio_fire_callbacks(&d->callbacks, port, fire);
	}
}

static void sim_gpio_notify(const struct device *port)
{
	struct sim_gpio_data *d = port->data;

	if (d->notify != NULL) {
		d->notify(port, d->notify_ctx);
	}
	sim_gpio_fire_interrupts(port);
}

static int sim_gpio_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	struct sim_gpio_data *d = port->data;
	uint32_t bit = BIT(pin);

	K_SPINLOCK(&d->lock) {
		if ((flags & GPIO_OUTPUT) != 0) {
			d->out_en |= bit;
			if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
				d->out |= bit;
			} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
				d->out &= ~bit;
			}
		} else {
			d->out_en &= ~bit;
		}
		if ((flags & GPIO_PULL_UP) != 0) {
			d->pull_up |= bit;
		} else {
			d->pull_up &= ~bit;
		}
	}

	sim_gpio_notify(port);
	return 0;
}

static int sim_gpio_port_get_raw(const struct device *port, gpio_port_value_t *value)
{
	*value = sim_gpio_resolve(port->data);
	return 0;
}

static int sim_gpio_port_set_masked_raw(const struct device *port, gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	struct sim_gpio_data *d = port->data;

	K_SPINLOCK(&d->lock) {
		d->out = (d->out & ~mask) | (value & mask);
	}
	sim_gpio_notify(port);
	return 0;
}

static int sim_gpio_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	return sim_gpio_port_set_masked_raw(port, pins, pins);
}

static int sim_gpio_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	return sim_gpio_port_set_masked_raw(port, pins, 0);
}

static int sim_gpio_port_toggle_bits(const struct device *port, gpio_port_pins_t pins)
{
	struct sim_gpio_data *d = port->data;

	K_SPINLOCK(&d->lock) {
		d->out ^= pins;
	}
	sim_gpio_notify(port);
	return 0;
}

static int sim_gpio_pin_interrupt_configure(const struct device *port, gpio_pin_t pin,
					    enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	struct sim_gpio_data *d = port->data;
	uint32_t bit = BIT(pin);

	K_SPINLOCK(&d->lock) {
		d->int_rise &= ~bit;
		d->int_fall &= ~bit;
		d->int_high &= ~bit;
		d->int_low &= ~bit;
		if (mode == GPIO_INT_MODE_EDGE) {
			if (trig == GPIO_INT_TRIG_HIGH || trig == GPIO_INT_TRIG_BOTH) {
				d->int_rise |= bit;
			}
			if (trig == GPIO_INT_TRIG_LOW || trig == GPIO_INT_TRIG_BOTH) {
				d->int_fall |= bit;
			}
		} else if (mode == GPIO_INT_MODE_LEVEL) {
			if (trig == GPIO_INT_TRIG_HIGH || trig == GPIO_INT_TRIG_BOTH) {
				d->int_high |= bit;
			}
			if (trig == GPIO_INT_TRIG_LOW || trig == GPIO_INT_TRIG_BOTH) {
				d->int_low |= bit;
			}
		}
	}
	d->last_in = sim_gpio_resolve(d);
	return 0;
}

static int sim_gpio_manage_callback(const struct device *port, struct gpio_callback *cb, bool set)
{
	struct sim_gpio_data *d = port->data;

	return gpio_manage_callback(&d->callbacks, cb, set);
}

static uint32_t sim_gpio_get_pending_int(const struct device *port)
{
	ARG_UNUSED(port);
	return 0;
}

static DEVICE_API(gpio, sim_gpio_api) = {
	.pin_configure = sim_gpio_pin_configure,
	.port_get_raw = sim_gpio_port_get_raw,
	.port_set_masked_raw = sim_gpio_port_set_masked_raw,
	.port_set_bits_raw = sim_gpio_port_set_bits_raw,
	.port_clear_bits_raw = sim_gpio_port_clear_bits_raw,
	.port_toggle_bits = sim_gpio_port_toggle_bits,
	.pin_interrupt_configure = sim_gpio_pin_interrupt_configure,
	.manage_callback = sim_gpio_manage_callback,
	.get_pending_int = sim_gpio_get_pending_int,
};

void sim_gpio_set_listener(const struct device *port, sim_gpio_notify_fn fn, void *ctx)
{
	struct sim_gpio_data *d = port->data;

	d->notify_ctx = ctx;
	d->notify = fn;
}

void sim_gpio_set_peer(const struct device *port, sim_gpio_peer_fn fn, void *ctx)
{
	struct sim_gpio_data *d = port->data;

	d->peer_ctx = ctx;
	d->peer = fn;
}

uint32_t sim_gpio_wire(const struct device *port)
{
	return sim_gpio_resolve(port->data);
}

uint32_t sim_gpio_out_latch(const struct device *port)
{
	struct sim_gpio_data *d = port->data;

	return d->out;
}

uint32_t sim_gpio_out_enable(const struct device *port)
{
	struct sim_gpio_data *d = port->data;

	return d->out_en;
}

void sim_gpio_force(const struct device *port, uint32_t mask, uint32_t levels)
{
	struct sim_gpio_data *d = port->data;

	K_SPINLOCK(&d->lock) {
		d->force_mask |= mask;
		d->force_val = (d->force_val & ~mask) | (levels & mask);
	}
	sim_gpio_fire_interrupts(port);
}

void sim_gpio_unforce(const struct device *port, uint32_t mask)
{
	struct sim_gpio_data *d = port->data;

	K_SPINLOCK(&d->lock) {
		d->force_mask &= ~mask;
	}
	sim_gpio_fire_interrupts(port);
}

void sim_gpio_poke(const struct device *port)
{
	sim_gpio_fire_interrupts(port);
}

#define SIM_GPIO_INIT(n)                                                                           \
	static const struct sim_gpio_config sim_gpio_config_##n = {                                \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n)},                   \
	};                                                                                         \
	static struct sim_gpio_data sim_gpio_data_##n;                                             \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &sim_gpio_data_##n, &sim_gpio_config_##n,             \
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, &sim_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_GPIO_INIT)
