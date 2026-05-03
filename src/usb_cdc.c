// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "usb_cdc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/sys_io.h>

/* STM32F4 96-bit Unique Device ID (RM0090 §39.1, base 0x1FFF7A10).
 * Encodes wafer coordinates + lot number. */
#define STM32_UID_BASE 0x1FFF7A10U

LOG_MODULE_REGISTER(usb_cdc, LOG_LEVEL_INF);

static const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

/* Log path is non-blocking: buffered here (drop-oldest), drained by cdc_drain_thread_fn(). */

#define CDC_LOG_BUF_SIZE 2048
RING_BUF_DECLARE(cdc_log_ringbuf, CDC_LOG_BUF_SIZE);
static atomic_t cdc_ready; /* 0 before DTR, 1 after — gates the drain thread */

/** Ring-buffer log sink for the CDC backend; drops oldest bytes when full. */
static int cdc_log_out_func(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	uint32_t space = ring_buf_space_get(&cdc_log_ringbuf);
	if (space < length) {
		uint8_t discard[64];
		uint32_t to_drop = length - space;
		while (to_drop > 0) {
			uint32_t n = MIN(to_drop, sizeof(discard));
			ring_buf_get(&cdc_log_ringbuf, discard, n);
			to_drop -= n;
		}
	}
	ring_buf_put(&cdc_log_ringbuf, data, length);
	return (int)length;
}

static uint8_t cdc_log_out_scratch[128];
LOG_OUTPUT_DEFINE(cdc_log_output, cdc_log_out_func,
		  cdc_log_out_scratch, sizeof(cdc_log_out_scratch));

static void cdc_backend_process(const struct log_backend *const backend,
				union log_msg_generic *msg)
{
	ARG_UNUSED(backend);
	uint32_t flags = LOG_OUTPUT_FLAG_TIMESTAMP | LOG_OUTPUT_FLAG_LEVEL;
	log_output_msg_process(&cdc_log_output, &msg->log, flags);
}

static const struct log_backend_api cdc_backend_api = {
	.process = cdc_backend_process,
};

LOG_BACKEND_DEFINE(cdc_backend, cdc_backend_api, true);

#define USB_VID 0x2fe3
#define USB_PID 0x0001

USBD_DEVICE_DEFINE(dmrtastic_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   USB_VID, USB_PID);

USBD_DESC_LANG_DEFINE(usb_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb_mfr, "DMRtastic");
USBD_DESC_PRODUCT_DEFINE(usb_product, "DMRtastic");

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

USBD_CONFIGURATION_DEFINE(usb_fs_config,
			  USB_SCD_SELF_POWERED,
			  125, &fs_cfg_desc);

/* Whole USB lifecycle runs in usb_cdc_thread_fn() so a stalled host can't block other code. */

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

	usbd_device_set_code_triple(&dmrtastic_usbd, USBD_SPEED_FS,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);

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

	atomic_set(&cdc_ready, 1);
	/* Drain thread now owns ring buffer → UART; just raise modem status. */

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
	if (ret) {
		LOG_WRN("Failed to set DCD, ret code %d", ret);
	}

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
	if (ret) {
		LOG_WRN("Failed to set DSR, ret code %d", ret);
	}
}

/** Run descriptor init, wait for DTR, then hand the UART to the drain thread. */
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

	LOG_INF("STM32 UID: %08x %08x %08x",
		(unsigned int)sys_read32(STM32_UID_BASE + 0x00U),
		(unsigned int)sys_read32(STM32_UID_BASE + 0x04U),
		(unsigned int)sys_read32(STM32_UID_BASE + 0x08U));

	usb_cdc_wait_dtr();
	usb_cdc_flush_and_enable();
	/* Thread exits here; the USB stack keeps running in its own worker contexts. */
}

#define USB_CDC_THREAD_STACK_SIZE 2048
#define USB_CDC_THREAD_PRIO       7

K_THREAD_DEFINE(usb_cdc_thread_id,
		USB_CDC_THREAD_STACK_SIZE,
		usb_cdc_thread_fn, NULL, NULL, NULL,
		USB_CDC_THREAD_PRIO, 0, 0);

/** Drain cdc_log_ringbuf to the UART once DTR is up; parks pre-DTR at 100ms. */
static void cdc_drain_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t buf[64];

	while (true) {
		if (!atomic_get(&cdc_ready)) {
			k_msleep(100);
			continue;
		}

		uint32_t n = ring_buf_get(&cdc_log_ringbuf, buf, sizeof(buf));
		if (n == 0) {
			k_msleep(10);
			continue;
		}

		for (uint32_t i = 0; i < n; i++) {
			uart_poll_out(uart_dev, buf[i]);
		}
	}
}

#define CDC_DRAIN_THREAD_STACK_SIZE 1024
#define CDC_DRAIN_THREAD_PRIO       9

K_THREAD_DEFINE(cdc_drain_thread_id,
		CDC_DRAIN_THREAD_STACK_SIZE,
		cdc_drain_thread_fn, NULL, NULL, NULL,
		CDC_DRAIN_THREAD_PRIO, 0, 0);
