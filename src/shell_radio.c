// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT
//
// Interactive register shell over USB CDC — see cmd_help() for the command list.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/radio/radio_baseband.h>
#include <drivers/radio/radio_transceiver.h>

#include "model/codeplug.h"

static const struct device *const uart_dev =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static const struct device *const trx =
	DEVICE_DT_GET(DT_NODELABEL(at1846s));
static const struct device *const bb =
	DEVICE_DT_GET(DT_NODELABEL(hr_c6000));
static const struct device *const rtc_dev =
	DEVICE_DT_GET(DT_NODELABEL(rtc));

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

/** Parse a bare hex value (no 0x prefix, up to 32 bits); false on malformed input. */
static bool parse_hex32(const char *s, uint32_t *out)
{
	if (!s || *s == '\0') {
		return false;
	}
	char *end;
	unsigned long v = strtoul(s, &end, 16);

	if (end == s || *end != '\0') {
		return false;
	}
	*out = (uint32_t)v;
	return true;
}

/** Parse a bare decimal int; false on malformed input. */
static bool parse_dec(const char *s, int *out)
{
	if (!s || *s == '\0') {
		return false;
	}
	char *end;
	long v = strtol(s, &end, 10);

	if (end == s || *end != '\0') {
		return false;
	}
	*out = (int)v;
	return true;
}

/** Print len bytes starting at display address base_addr, 16 bytes/line. */
static void shell_hexdump(uint32_t base_addr, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i += 16) {
		size_t n = MIN((size_t)16, len - i);

		shell_printf("%06lX:", (unsigned long)(base_addr + i));
		for (size_t j = 0; j < n; j++) {
			shell_printf(" %02X", data[i + j]);
		}
		shell_puts("\r\n");
	}
}

/** Format a decoded CSS value, e.g. "203.5Hz" / "D023N" / "D023I" / "off". */
static const char *format_css(struct cp_css css, char *buf, size_t buflen)
{
	if (css.type == CP_CSS_CTCSS) {
		snprintf(buf, buflen, "%u.%uHz", css.value / 10, css.value % 10);
	} else if (css.type == CP_CSS_DCS) {
		snprintf(buf, buflen, "D%03o%c", css.value, css.inverted ? 'I' : 'N');
	} else {
		snprintf(buf, buflen, "off");
	}
	return buf;
}

/** Format a signed x1e4-degree fixed-point value as e.g. "-123.4567", no floating point. */
static const char *format_latlon(int32_t v, char *buf, size_t buflen)
{
	bool neg = v < 0;
	int32_t abs_v = neg ? -v : v;

	snprintf(buf, buflen, "%s%ld.%04ld", neg ? "-" : "",
		 (long)(abs_v / 10000), (long)(abs_v % 10000));
	return buf;
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

/** "cp list" — print every known codeplug region: name, offset, size. */
static void cmd_cp_list(void)
{
	struct cp_region_info regions[20];
	int n = codeplug_list_regions(regions, ARRAY_SIZE(regions));

	for (int i = 0; i < n; i++) {
		shell_printf("%-24s off=0x%06lX size=0x%04lX\r\n",
			     regions[i].name, (unsigned long)regions[i].offset,
			     (unsigned long)regions[i].size);
	}
}

/** "cp dump <addr> <len>" — raw hexdump of an absolute chip address range. */
static void cmd_cp_dump(char *args)
{
	char *arg1 = strtok(args, " \t");
	char *arg2 = strtok(NULL, " \t");
	uint32_t addr, len;
	uint8_t buf[16];

	if (!parse_hex32(arg1, &addr) || !parse_hex32(arg2, &len) || len == 0) {
		shell_puts("ERR: cp dump <addr> <len>\r\n");
		return;
	}

	while (len > 0) {
		size_t n = MIN((uint32_t)sizeof(buf), len);
		int rc = codeplug_raw_read(addr, buf, n);

		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
			return;
		}
		shell_hexdump(addr, buf, n);
		addr += n;
		len -= n;
	}
}

/** Raw hexdump of a named region looked up via codeplug_list_regions(). */
static void cp_dump_named_region(const char *name)
{
	struct cp_region_info regions[20];
	int n = codeplug_list_regions(regions, ARRAY_SIZE(regions));

	for (int i = 0; i < n; i++) {
		if (strcmp(regions[i].name, name) != 0) {
			continue;
		}

		uint32_t off = regions[i].offset;
		uint32_t remaining = regions[i].size;
		uint8_t buf[16];

		while (remaining > 0) {
			size_t chunk = MIN((uint32_t)sizeof(buf), remaining);
			int rc = codeplug_raw_read(off, buf, chunk);

			if (rc < 0) {
				shell_printf("ERR: %d\r\n", rc);
				return;
			}
			shell_hexdump(off, buf, chunk);
			off += chunk;
			remaining -= chunk;
		}
		return;
	}
	shell_printf("ERR: unknown region '%s'\r\n", name);
}

/** "cp region <name> [idx]" — typed decode of one named region, or a raw dump. */
static void cmd_cp_region(char *args)
{
	char *name = strtok(args, " \t");
	char *idx_str = strtok(NULL, " \t");
	int idx = idx_str ? (int)strtoul(idx_str, NULL, 10) : 0;
	int rc;

	if (!name) {
		shell_puts("ERR: cp region <name> [idx]\r\n");
		return;
	}

	if (strcmp(name, "general-settings") == 0) {
		struct cp_general_settings s;

		rc = codeplug_get_general_settings(&s);
		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
			return;
		}
		shell_printf("radioName=%.8s radioId=%lu\r\n",
			     s.radioName, (unsigned long)s.radioId);
	} else if (strcmp(name, "device-info") == 0) {
		struct cp_device_info d;

		rc = codeplug_get_device_info(&d);
		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
			return;
		}
		shell_printf("model=%.8s sn=%.16s hw=%.8s fw=%.8s\r\n",
			     d.model, d.sn, d.hardwareVer, d.firmwareVer);
		shell_printf("uhf=%u-%uMHz vhf=%u-%uMHz\r\n",
			     d.minUHFFreq, d.maxUHFFreq, d.minVHFFreq, d.maxVHFFreq);
	} else if (strcmp(name, "channel") == 0) {
		struct cp_channel ch;

		rc = codeplug_get_channel(idx, &ch);
		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
			return;
		}
		if (!codeplug_channel_is_in_use(&ch)) {
			shell_puts("slot unused\r\n");
			return;
		}

		char rx_buf[12], tx_buf[12], lat_buf[16], lon_buf[16];
		uint8_t lat_raw[3] = { ch.locationLat0, ch.locationLat1, ch.locationLat2 };
		uint8_t lon_raw[3] = { ch.locationLon0, ch.locationLon1, ch.locationLon2 };

		shell_printf("name=%.16s rx=%lu tx=%lu mode=%u pwr=%u\r\n",
			     ch.name, (unsigned long)ch.rxFreq,
			     (unsigned long)ch.txFreq, ch.chMode, ch.power);
		shell_printf("rxTone=%s txTone=%s\r\n",
			     format_css(codeplug_decode_css(ch.rxTone), rx_buf, sizeof(rx_buf)),
			     format_css(codeplug_decode_css(ch.txTone), tx_buf, sizeof(tx_buf)));
		shell_printf("lat=%s lon=%s\r\n",
			     format_latlon(codeplug_decode_latlon(lat_raw), lat_buf, sizeof(lat_buf)),
			     format_latlon(codeplug_decode_latlon(lon_raw), lon_buf, sizeof(lon_buf)));
	} else if (strcmp(name, "contact") == 0) {
		struct cp_contact c;

		rc = codeplug_get_contact(idx, &c);
		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
			return;
		}
		if (!codeplug_contact_is_in_use(&c)) {
			shell_puts("slot unused\r\n");
			return;
		}
		shell_printf("name=%.16s tg=%lu type=%u\r\n",
			     c.name, (unsigned long)c.tgNumber, c.callType);
	} else if (strcmp(name, "zone") == 0) {
		struct cp_zone z;
		int channels_per_zone;

		rc = codeplug_get_zone(idx, &z, &channels_per_zone);
		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
			return;
		}
		shell_printf("name=%.16s channels_per_zone=%d\r\n", z.name, channels_per_zone);
	} else {
		cp_dump_named_region(name);
	}
}

/** "cp settings" — on-flash settings block: magic number, then best-effort decode or dump. */
static void cmd_cp_settings(void)
{
	uint8_t buf[256];
	uint32_t magic;
	int rc = codeplug_get_nv_settings_raw(buf, sizeof(buf), &magic);

	if (rc < 0) {
		shell_printf("ERR: %d\r\n", rc);
		return;
	}
	shell_printf("magic=0x%04lX (latest known=0x%04X)\r\n",
		     (unsigned long)magic, CP_NV_SETTINGS_MAGIC_LATEST);

	if (magic == CP_NV_SETTINGS_MAGIC_LATEST) {
		struct cp_nv_settings *s = (struct cp_nv_settings *)buf;

		shell_printf("backlightMode=%u backLightTimeout=%u txPowerLevel=%u\r\n",
			     s->backlightMode, s->backLightTimeout, s->txPowerLevel);
	} else {
		shell_puts("magic mismatch -- raw dump:\r\n");
		shell_hexdump(0, buf, MIN(sizeof(buf), (size_t)64));
	}
}

/** "cp info" — JEDEC ID, device info, and a calibration-marker sanity check. */
static void cmd_cp_info(void)
{
	uint8_t id[3];
	int rc = codeplug_get_jedec_id(id);

	if (rc < 0) {
		shell_printf("jedec: ERR %d\r\n", rc);
	} else {
		shell_printf("jedec: %02X %02X %02X\r\n", id[0], id[1], id[2]);
	}

	struct cp_device_info info;

	rc = codeplug_get_device_info(&info);
	if (rc < 0) {
		shell_printf("device-info: ERR %d\r\n", rc);
	} else {
		shell_printf("model=%.8s sn=%.16s fw=%.8s\r\n",
			     info.model, info.sn, info.firmwareVer);
	}

	static const uint8_t cal_marker[8] = {0x00, 0x25, 0x00, 0x40, 0x00, 0x45, 0x01, 0x40};
	struct cp_calibration cal;

	rc = codeplug_get_calibration(&cal);
	if (rc < 0) {
		shell_printf("calibration marker: ERR %d\r\n", rc);
	} else {
		bool ok = memcmp(cal.uhfCalFreqs[0], cal_marker, sizeof(cal_marker)) == 0;

		shell_printf("calibration marker: %s\r\n", ok ? "OK" : "MISMATCH");
	}
}

/** "cp dump|list|region|settings|info ..." — codeplug flash inspection. */
static void cmd_cp(char *args)
{
	char *sub = strtok(args, " \t");
	char *rest = strtok(NULL, "");

	if (!sub) {
		shell_puts("ERR: cp dump|list|region|settings|info ...\r\n");
		return;
	}

	if (strcmp(sub, "dump") == 0) {
		cmd_cp_dump(rest ? rest : "");
	} else if (strcmp(sub, "list") == 0) {
		cmd_cp_list();
	} else if (strcmp(sub, "region") == 0) {
		cmd_cp_region(rest ? rest : "");
	} else if (strcmp(sub, "settings") == 0) {
		cmd_cp_settings();
	} else if (strcmp(sub, "info") == 0) {
		cmd_cp_info();
	} else {
		shell_puts("ERR: cp dump|list|region|settings|info ...\r\n");
	}
}

/** Sakamoto's algorithm — day of week for a Gregorian date (0 = Sunday), matching struct rtc_time.
 * The STM32 RTC driver rejects tm_wday == -1 (unlike the "unknown" allowance in the struct's own
 * doc comment), so this can't be left unset on a "rtc w". */
static int day_of_week(int year, int mon, int day)
{
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	if (mon < 3) {
		year -= 1;
	}
	return (year + year / 4 - year / 100 + year / 400 + t[mon - 1] + day) % 7;
}

/** "rtc r" / "rtc w <year> <mon> <day> <hour> <min> <sec>" — read/set the hardware RTC. */
static void cmd_rtc(char *args)
{
	char *sub = strtok(args, " \t");

	if (!sub) {
		shell_puts("ERR: rtc r|w ...\r\n");
		return;
	}

	if (strcmp(sub, "r") == 0) {
		struct rtc_time tm;
		int rc = rtc_get_time(rtc_dev, &tm);

		if (rc < 0) {
			shell_printf("ERR: %d\r\n", rc);
		} else {
			shell_printf("%04d-%02d-%02d %02d:%02d:%02d\r\n",
				     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				     tm.tm_hour, tm.tm_min, tm.tm_sec);
		}
	} else if (strcmp(sub, "w") == 0) {
		int year, mon, day, hour, min, sec;

		if (!parse_dec(strtok(NULL, " \t"), &year) ||
		    !parse_dec(strtok(NULL, " \t"), &mon)  ||
		    !parse_dec(strtok(NULL, " \t"), &day)  ||
		    !parse_dec(strtok(NULL, " \t"), &hour) ||
		    !parse_dec(strtok(NULL, " \t"), &min)  ||
		    !parse_dec(strtok(NULL, " \t"), &sec)) {
			shell_puts("ERR: rtc w <year> <mon> <day> <hour> <min> <sec>\r\n");
			return;
		}

		struct rtc_time tm = {
			.tm_sec  = sec,
			.tm_min  = min,
			.tm_hour = hour,
			.tm_mday = day,
			.tm_mon  = mon - 1,
			.tm_year = year - 1900,
			.tm_wday = day_of_week(year, mon, day),
			.tm_yday = -1,
			.tm_isdst = -1,
			.tm_nsec = 0,
		};
		int rc = rtc_set_time(rtc_dev, &tm);

		shell_printf("%s\r\n", rc < 0 ? "ERR writing" : "OK");
	} else {
		shell_puts("ERR: rtc r|w ...\r\n");
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
		"cp dump <addr> <len>     raw codeplug flash hexdump\r\n"
		"cp list                  list known codeplug regions\r\n"
		"cp region <name> [idx]   decode a named codeplug region\r\n"
		"cp settings              on-flash settings block + magic number\r\n"
		"cp info                  JEDEC ID, device info, calibration check\r\n"
		"rtc r                    read the hardware RTC date/time\r\n"
		"rtc w <Y> <M> <D> <h> <m> <s>  set the hardware RTC date/time\r\n"
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
	} else if (strcmp(cmd, "cp") == 0) {
		cmd_cp(rest ? rest : "");
	} else if (strcmp(cmd, "rtc") == 0) {
		cmd_rtc(rest ? rest : "");
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
