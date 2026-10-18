// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/* Stand-in for the USB CDC ACM UART that forwards every operation to the console pty UART. */

#define DT_DRV_COMPAT zephyr_cdc_acm_uart

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#define BACKEND DEVICE_DT_GET(DT_NODELABEL(uart0))

static uart_irq_callback_user_data_t s_cb;
static void *s_cb_data;
static const struct device *s_self;

static void trampoline(const struct device *backend, void *user_data)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(user_data);
	if (s_cb != NULL) {
		s_cb(s_self, s_cb_data);
	}
}

static int fwd_poll_in(const struct device *dev, unsigned char *c)
{
	ARG_UNUSED(dev);
	return uart_poll_in(BACKEND, c);
}

static void fwd_poll_out(const struct device *dev, unsigned char c)
{
	ARG_UNUSED(dev);
	uart_poll_out(BACKEND, c);
}

static int fwd_err_check(const struct device *dev)
{
	ARG_UNUSED(dev);
	return uart_err_check(BACKEND);
}

static int fwd_fifo_fill(const struct device *dev, const uint8_t *buf, int len)
{
	ARG_UNUSED(dev);
	return uart_fifo_fill(BACKEND, buf, len);
}

static int fwd_fifo_read(const struct device *dev, uint8_t *buf, const int len)
{
	ARG_UNUSED(dev);
	return uart_fifo_read(BACKEND, buf, len);
}

static void fwd_irq_tx_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
	uart_irq_tx_enable(BACKEND);
}

static void fwd_irq_tx_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	uart_irq_tx_disable(BACKEND);
}

static int fwd_irq_tx_ready(const struct device *dev)
{
	ARG_UNUSED(dev);
	return uart_irq_tx_ready(BACKEND);
}

static void fwd_irq_rx_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
	uart_irq_rx_enable(BACKEND);
}

static void fwd_irq_rx_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	uart_irq_rx_disable(BACKEND);
}

static int fwd_irq_tx_complete(const struct device *dev)
{
	ARG_UNUSED(dev);
	return uart_irq_tx_complete(BACKEND);
}

static int fwd_irq_rx_ready(const struct device *dev)
{
	ARG_UNUSED(dev);
	return uart_irq_rx_ready(BACKEND);
}

static void fwd_irq_err_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
	uart_irq_err_enable(BACKEND);
}

static void fwd_irq_err_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	uart_irq_err_disable(BACKEND);
}

static int fwd_irq_is_pending(const struct device *dev)
{
	ARG_UNUSED(dev);
	return uart_irq_is_pending(BACKEND);
}

static int fwd_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
	return uart_irq_update(BACKEND);
}

static void fwd_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
				 void *user_data)
{
	s_self = dev;
	s_cb = cb;
	s_cb_data = user_data;
	uart_irq_callback_user_data_set(BACKEND, trampoline, NULL);
}

static int fwd_line_ctrl_set(const struct device *dev, uint32_t ctrl, uint32_t val)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ctrl);
	ARG_UNUSED(val);
	return 0;
}

static int fwd_line_ctrl_get(const struct device *dev, uint32_t ctrl, uint32_t *val)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ctrl);
	*val = 0;
	return 0;
}

static DEVICE_API(uart, sim_cdc_api) = {
	.poll_in = fwd_poll_in,
	.poll_out = fwd_poll_out,
	.err_check = fwd_err_check,
	.fifo_fill = fwd_fifo_fill,
	.fifo_read = fwd_fifo_read,
	.irq_tx_enable = fwd_irq_tx_enable,
	.irq_tx_disable = fwd_irq_tx_disable,
	.irq_tx_ready = fwd_irq_tx_ready,
	.irq_rx_enable = fwd_irq_rx_enable,
	.irq_rx_disable = fwd_irq_rx_disable,
	.irq_tx_complete = fwd_irq_tx_complete,
	.irq_rx_ready = fwd_irq_rx_ready,
	.irq_err_enable = fwd_irq_err_enable,
	.irq_err_disable = fwd_irq_err_disable,
	.irq_is_pending = fwd_irq_is_pending,
	.irq_update = fwd_irq_update,
	.irq_callback_set = fwd_irq_callback_set,
	.line_ctrl_set = fwd_line_ctrl_set,
	.line_ctrl_get = fwd_line_ctrl_get,
};

#define SIM_CDC_INIT(n)                                                                            \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                              \
			      CONFIG_SERIAL_INIT_PRIORITY, &sim_cdc_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_CDC_INIT)
