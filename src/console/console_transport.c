// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "console_transport.h"
#include "console_log_backend.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

/* STM32F4 96-bit Unique Device ID (RM0090 §39.1, base 0x1FFF7A10).
 * Encodes wafer coordinates + lot number.
 */
#define STM32_UID_BASE 0x1FFF7A10U

LOG_MODULE_REGISTER(console_transport, LOG_LEVEL_INF);

static const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

/* ---- TX: single mutex-guarded path, shared by the log backend and every
 * interactive command's output -- prevents a log line and command output from
 * interleaving mid-line on the wire.
 */

static K_MUTEX_DEFINE(tx_mutex);

void console_transport_write(const void *data, size_t len)
{
	const uint8_t *p = data;

	k_mutex_lock(&tx_mutex, K_FOREVER);
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, p[i]);
	}
	k_mutex_unlock(&tx_mutex);
}

void console_transport_putc(char c)
{
	console_transport_write(&c, 1);
}

void console_transport_puts(const char *s)
{
	console_transport_write(s, strlen(s));
}

void console_transport_vprintf(const char *fmt, va_list ap)
{
	char buf[128];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);

	if (n > 0) {
		console_transport_write(buf, MIN((size_t)n, sizeof(buf) - 1));
	}
}

void console_transport_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	console_transport_vprintf(fmt, ap);
	va_end(ap);
}

/* ---- RX: ring buffer fed by the UART ISR, drained by console_transport_getc(). */

#define RX_BUF_SIZE 256
RING_BUF_DECLARE(rx_ringbuf, RX_BUF_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);

static void uart_irq_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t buf[16];
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n > 0) {
			ring_buf_put(&rx_ringbuf, buf, n);
			k_sem_give(&rx_sem);
		}
	}
}

void console_transport_rx_start(void)
{
	uart_irq_callback_set(uart_dev, uart_irq_cb);
	uart_irq_rx_enable(uart_dev);
}

int console_transport_getc(void)
{
	uint8_t c;

	while (ring_buf_get(&rx_ringbuf, &c, 1) == 0) {
		k_sem_take(&rx_sem, K_FOREVER);
	}
	return (int)c;
}

/* ---- USB CDC-ACM device lifecycle: descriptors, enumeration, DTR handshake.
 * Runs entirely in its own thread so a stalled host can't block other code.
 */

#define USB_VID 0x2fe3
#define USB_PID 0x0001

USBD_DEVICE_DEFINE(dmrtastic_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), USB_VID, USB_PID);

USBD_DESC_LANG_DEFINE(usb_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb_mfr, "DMRtastic");
USBD_DESC_PRODUCT_DEFINE(usb_product, "DMRtastic");

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

USBD_CONFIGURATION_DEFINE(usb_fs_config, USB_SCD_SELF_POWERED, 125, &fs_cfg_desc);

K_SEM_DEFINE(dtr_sem, 0, 1);

static inline void print_baudrate(const struct device *dev)
{
	uint32_t baudrate;
	int ret;

	ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret) {
		LOG_WRN("Failed to get baudrate, ret code %d", ret);
	} else {
		LOG_INF("Baudrate %u", baudrate);
	}
}

/** Dispatch USBD messages: VBUS enable/disable, DTR detection, line coding. */
static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING ||
	    msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		LOG_DBG("USBD message: %s", usbd_msg_type_string(msg->type));
	} else {
		LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));
	}

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		print_baudrate(msg->dev);
	}

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_sem_give(&dtr_sem);
		}
	}
}

/** Register descriptors/classes and enable the USBD device. */
static int usb_cdc_init(void)
{
	int err;

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("CDC ACM device not ready");
		return -ENODEV;
	}

	err = usbd_add_descriptor(&dmrtastic_usbd, &usb_lang);
	if (err) {
		LOG_ERR("Failed to add language descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&dmrtastic_usbd, &usb_mfr);
	if (err) {
		LOG_ERR("Failed to add manufacturer descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&dmrtastic_usbd, &usb_product);
	if (err) {
		LOG_ERR("Failed to add product descriptor (%d)", err);
		return err;
	}

	err = usbd_add_configuration(&dmrtastic_usbd, USBD_SPEED_FS, &usb_fs_config);
	if (err) {
		LOG_ERR("Failed to add FS configuration (%d)", err);
		return err;
	}

	err = usbd_register_all_classes(&dmrtastic_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("Failed to register classes (%d)", err);
		return err;
	}

	usbd_device_set_code_triple(&dmrtastic_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02,
				    0x01);

	err = usbd_msg_register_cb(&dmrtastic_usbd, usb_msg_cb);
	if (err) {
		LOG_ERR("Failed to register message callback (%d)", err);
		return err;
	}

	err = usbd_init(&dmrtastic_usbd);
	if (err) {
		LOG_ERR("Failed to initialize USB device (%d)", err);
		return err;
	}

	if (!usbd_can_detect_vbus(&dmrtastic_usbd)) {
		err = usbd_enable(&dmrtastic_usbd);
		if (err) {
			LOG_ERR("Failed to enable device support (%d)", err);
			return err;
		}
	}

	LOG_INF("USB device support enabled");
	return 0;
}

static void usb_cdc_wait_dtr(void)
{
	k_sem_take(&dtr_sem, K_FOREVER);
}

static void usb_cdc_flush_and_enable(void)
{
	int ret;

	console_log_backend_notify_ready();

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
	if (ret) {
		LOG_WRN("Failed to set DCD, ret code %d", ret);
	}

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
	if (ret) {
		LOG_WRN("Failed to set DSR, ret code %d", ret);
	}
}

/** Run descriptor init, wait for DTR, then raise modem status once the host has the port open. */
static void usb_cdc_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int ret = usb_cdc_init();
	if (ret != 0) {
		LOG_ERR("USB init failed (%d) — USB thread exiting", ret);
		return;
	}

	LOG_INF("STM32 UID: %08x %08x %08x", (unsigned int)sys_read32(STM32_UID_BASE + 0x00U),
		(unsigned int)sys_read32(STM32_UID_BASE + 0x04U),
		(unsigned int)sys_read32(STM32_UID_BASE + 0x08U));

	usb_cdc_wait_dtr();
	usb_cdc_flush_and_enable();
	/* Thread exits here; the USB stack keeps running in its own worker contexts. */
}

#define USB_CDC_THREAD_STACK_SIZE 2048
#define USB_CDC_THREAD_PRIO       7

K_THREAD_DEFINE(usb_cdc_thread_id, USB_CDC_THREAD_STACK_SIZE, usb_cdc_thread_fn, NULL, NULL, NULL,
		USB_CDC_THREAD_PRIO, 0, 0);
