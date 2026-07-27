// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT
//
// Interactive register shell over USB CDC — see cmd_help() for the command list.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/ring_buffer.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/radio/radio_baseband.h>
#include <drivers/radio/radio_transceiver.h>

static const struct device *const uart_dev =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static const struct device *const trx =
	DEVICE_DT_GET(DT_NODELABEL(at1846s));
static const struct device *const bb =
	DEVICE_DT_GET(DT_NODELABEL(hr_c6000));

/* ---- RX ring buffer -------------------------------------------------- */

#define RX_BUF_SIZE 256
RING_BUF_DECLARE(rx_ringbuf, RX_BUF_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);

/** UART ISR: drain the RX FIFO into rx_ringbuf and wake shell_getc(). */
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

static int shell_getc(void)
{
	uint8_t c;

	while (ring_buf_get(&rx_ringbuf, &c, 1) == 0) {
		k_sem_take(&rx_sem, K_FOREVER);
	}
	return (int)c;
}

/* ---- output helpers -------------------------------------------------- */

static void shell_putc(char c)
{
	uart_poll_out(uart_dev, c);
}

static void shell_puts(const char *s)
{
	while (*s) {
		shell_putc(*s++);
	}
}

static void shell_printf(const char *fmt, ...)
{
	char buf[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	shell_puts(buf);
}

/* ---- command handlers ------------------------------------------------ */

/** Parse a bare hex byte (no 0x prefix); returns false on any malformed input. */
static bool parse_hex8(const char *s, uint8_t *out)
{
	if (!s || *s == '\0') {
		return false;
	}
	char *end;
	unsigned long v = strtoul(s, &end, 16);

	if (end == s || *end != '\0' || v > 0xFF) {
		return false;
	}
	*out = (uint8_t)v;
	return true;
}

/** "at r <reg>" / "at w <reg> <hi> <lo>" — read/write an AT1846S register. */
static void cmd_at(char *args)
{
	const struct radio_trx_api *api =
		(const struct radio_trx_api *)trx->api;
	char *sub  = strtok(args, " \t");
	char *arg1 = strtok(NULL, " \t");
	char *arg2 = strtok(NULL, " \t");
	char *arg3 = strtok(NULL, " \t");
	uint8_t reg, hi, lo;

	if (!sub) {
		shell_puts("ERR: at r|w ...\r\n");
		return;
	}

	if (strcmp(sub, "r") == 0) {
		if (!parse_hex8(arg1, &reg)) {
			shell_puts("ERR: at r <reg>\r\n");
			return;
		}
		int rc = api->read_raw(trx, reg, &hi, &lo);

		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
		} else {
			shell_printf("0x%02X = %02X%02X\r\n", reg, hi, lo);
		}
	} else if (strcmp(sub, "w") == 0) {
		if (!parse_hex8(arg1, &reg) ||
		    !parse_hex8(arg2, &hi)  ||
		    !parse_hex8(arg3, &lo)) {
			shell_puts("ERR: at w <reg> <hi> <lo>\r\n");
			return;
		}
		int rc = api->write_raw(trx, reg, hi, lo);

		shell_printf("%s 0x%02X = %02X%02X\r\n",
			     rc < 0 ? "ERR writing" : "OK", reg, hi, lo);
	} else {
		shell_puts("ERR: at r|w ...\r\n");
	}
}

/** "hc r <page> <reg>" / "hc w <page> <reg> <val>" — read/write an HR-C6000 register. */
static void cmd_hc(char *args)
{
	const struct radio_bb_api *api =
		(const struct radio_bb_api *)bb->api;
	char *sub  = strtok(args, " \t");
	char *arg1 = strtok(NULL, " \t");
	char *arg2 = strtok(NULL, " \t");
	char *arg3 = strtok(NULL, " \t");
	uint8_t page, reg, val;

	if (!sub) {
		shell_puts("ERR: hc r|w ...\r\n");
		return;
	}

	if (strcmp(sub, "r") == 0) {
		if (!parse_hex8(arg1, &page) || !parse_hex8(arg2, &reg)) {
			shell_puts("ERR: hc r <page> <reg>\r\n");
			return;
		}
		int rc = api->read_raw(bb, page, reg, &val);

		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
		} else {
			shell_printf("p%02X:0x%02X = %02X\r\n", page, reg, val);
		}
	} else if (strcmp(sub, "w") == 0) {
		if (!parse_hex8(arg1, &page) ||
		    !parse_hex8(arg2, &reg)  ||
		    !parse_hex8(arg3, &val)) {
			shell_puts("ERR: hc w <page> <reg> <val>\r\n");
			return;
		}
		int rc = api->write_raw(bb, page, reg, val);

		shell_printf("%s p%02X:0x%02X = %02X\r\n",
			     rc < 0 ? "ERR writing" : "OK", page, reg, val);
	} else {
		shell_puts("ERR: hc r|w ...\r\n");
	}
}

static void cmd_rssi(void)
{
	const struct radio_trx_api *api =
		(const struct radio_trx_api *)trx->api;
	uint8_t hi, lo;
	int rc = api->read_raw(trx, 0x1B, &hi, &lo);

	if (rc < 0) {
		shell_printf("ERR: %d\r\n", rc);
	} else {
		/* hi = noise indicator (lower = stronger carrier/quieting)
		 * lo = signal/RSSI indicator */
		shell_printf("RSSI 0x1B: noise=%02X signal=%02X\r\n", hi, lo);
	}
}

static void cmd_reboot(void)
{
	shell_puts("rebooting...\r\n");
	/* Let the bytes above actually leave the FIFO before reset. */
	k_msleep(50);
	sys_reboot(SYS_REBOOT_WARM);
}

static void cmd_help(void)
{
	shell_puts(
		"at r <reg>               read AT1846S reg (hex)\r\n"
		"at w <reg> <hi> <lo>     write AT1846S reg\r\n"
		"hc r <page> <reg>        read HR-C6000 reg\r\n"
		"hc w <page> <reg> <val>  write HR-C6000 reg\r\n"
		"rssi                     AT1846S 0x1B noise/signal\r\n"
		"reboot                   warm-reset the MCU\r\n"
		"help                     this list\r\n"
	);
}

/** Route one input line to its command handler by first token. */
static void dispatch(char *line)
{
	char *cmd = strtok(line, " \t");

	if (!cmd || cmd[0] == '\0') {
		return;
	}
	char *rest = strtok(NULL, "");

	if (strcmp(cmd, "at") == 0) {
		cmd_at(rest ? rest : "");
	} else if (strcmp(cmd, "hc") == 0) {
		cmd_hc(rest ? rest : "");
	} else if (strcmp(cmd, "rssi") == 0) {
		cmd_rssi();
	} else if (strcmp(cmd, "reboot") == 0) {
		cmd_reboot();
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		cmd_help();
	} else {
		shell_printf("ERR: unknown command '%s' (try 'help')\r\n", cmd);
	}
}

/* ---- command reader thread ------------------------------------------- */

#define LINE_BUF 80

/** Line-editing read loop: buffer chars until CR/LF, then dispatch(). */
static void shell_radio_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uart_irq_callback_set(uart_dev, uart_irq_cb);
	uart_irq_rx_enable(uart_dev);

	char buf[LINE_BUF];
	int  pos = 0;

	while (true) {
		int c = shell_getc();

		if (c == '\r' || c == '\n') {
			if (pos > 0) {
				buf[pos] = '\0';
				pos = 0;
				dispatch(buf);
			}
		} else if (c == '\b' || c == 0x7F) {
			if (pos > 0) {
				pos--;
			}
		} else if (pos < LINE_BUF - 1) {
			buf[pos++] = (char)c;
		}
	}
}

#define SHELL_RADIO_STACK 1536
#define SHELL_RADIO_PRIO  10

K_THREAD_DEFINE(shell_radio_tid,
		SHELL_RADIO_STACK,
		shell_radio_thread_fn, NULL, NULL, NULL,
		SHELL_RADIO_PRIO, 0, 0);
