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

LOG_MODULE_REGISTER(usb_cdc, LOG_LEVEL_INF);

/* -----------------------------------------------------------------------
 * CDC ACM UART device
 * ----------------------------------------------------------------------- */

static const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

/* -----------------------------------------------------------------------
 * CDC-ACM log backend — buffers to ring buf before DTR, then writes direct
 * ----------------------------------------------------------------------- */

#define CDC_LOG_PRE_BUF_SIZE 2048
RING_BUF_DECLARE(cdc_pre_ringbuf, CDC_LOG_PRE_BUF_SIZE);
static atomic_t cdc_ready; /* 0 = buffer pre-DTR, 1 = write directly */

static int cdc_log_out_func(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	if (!atomic_get(&cdc_ready)) {
		uint32_t space = ring_buf_space_get(&cdc_pre_ringbuf);
		if (space < length) {
			uint8_t discard[64];
			uint32_t to_drop = length - space;
			while (to_drop > 0) {
				uint32_t n = MIN(to_drop, sizeof(discard));
				ring_buf_get(&cdc_pre_ringbuf, discard, n);
				to_drop -= n;
			}
		}
		ring_buf_put(&cdc_pre_ringbuf, data, length);
	} else {
		for (size_t i = 0; i < length; i++) {
			uart_poll_out(uart_dev, data[i]);
		}
	}
	return (int)length;
}

static void cdc_log_playback(void)
{
	uint8_t buf[64];
	uint32_t n;

	while ((n = ring_buf_get(&cdc_pre_ringbuf, buf, sizeof(buf))) > 0) {
		for (uint32_t i = 0; i < n; i++) {
			uart_poll_out(uart_dev, buf[i]);
		}
	}
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

/* -----------------------------------------------------------------------
 * USB device descriptors
 * ----------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------
 * USB CDC-ACM initialization
 * ----------------------------------------------------------------------- */

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

static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

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

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		print_baudrate(msg->dev);
	}
}

int usb_cdc_init(void)
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

void usb_cdc_wait_dtr(void)
{
	k_sem_take(&dtr_sem, K_FOREVER);
}

void usb_cdc_flush_and_enable(void)
{
	int ret;

	cdc_log_playback();
	atomic_set(&cdc_ready, 1);

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
	if (ret) {
		LOG_WRN("Failed to set DCD, ret code %d", ret);
	}

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
	if (ret) {
		LOG_WRN("Failed to set DSR, ret code %d", ret);
	}

	k_msleep(100);
}
