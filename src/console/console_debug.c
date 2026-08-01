// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/*
 * Hardware-direct debug commands: register-level poking for bring-up. Deliberately
 * bypasses the business-level Model/Controller layers the LVGL UI goes through --
 * every DEVICE_DT_GET for the transceiver/baseband/RTC lives in model/radio_debug.c,
 * this file only parses args, calls it, and formats output.
 */

#include "console_debug.h"
#include "console_util.h"
#include "console_transport.h"

#include "model/codeplug.h"
#include "model/radio_debug.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

/** "at r <reg>" / "at w <reg> <hi> <lo>" — read/write an AT1846S register. */
static void cmd_at(char *args)
{
	char *sub = strtok(args, " \t");
	char *arg1 = strtok(NULL, " \t");
	char *arg2 = strtok(NULL, " \t");
	char *arg3 = strtok(NULL, " \t");
	uint8_t reg, hi, lo;

	if (!sub) {
		console_transport_puts("ERR: at r|w ...\r\n");
		return;
	}

	if (strcmp(sub, "r") == 0) {
		if (!console_parse_hex8(arg1, &reg)) {
			console_transport_puts("ERR: at r <reg>\r\n");
			return;
		}
		int rc = radio_debug_at_read(reg, &hi, &lo);

		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
		} else {
			console_transport_printf("0x%02X = %02X%02X\r\n", reg, hi, lo);
		}
	} else if (strcmp(sub, "w") == 0) {
		if (!console_parse_hex8(arg1, &reg) || !console_parse_hex8(arg2, &hi) ||
		    !console_parse_hex8(arg3, &lo)) {
			console_transport_puts("ERR: at w <reg> <hi> <lo>\r\n");
			return;
		}
		int rc = radio_debug_at_write(reg, hi, lo);

		console_transport_printf("%s 0x%02X = %02X%02X\r\n", rc < 0 ? "ERR writing" : "OK",
					 reg, hi, lo);
	} else {
		console_transport_puts("ERR: at r|w ...\r\n");
	}
}

/** "hc r <page> <reg>" / "hc w <page> <reg> <val>" — read/write an HR-C6000 register. */
static void cmd_hc(char *args)
{
	char *sub = strtok(args, " \t");
	char *arg1 = strtok(NULL, " \t");
	char *arg2 = strtok(NULL, " \t");
	char *arg3 = strtok(NULL, " \t");
	uint8_t page, reg, val;

	if (!sub) {
		console_transport_puts("ERR: hc r|w ...\r\n");
		return;
	}

	if (strcmp(sub, "r") == 0) {
		if (!console_parse_hex8(arg1, &page) || !console_parse_hex8(arg2, &reg)) {
			console_transport_puts("ERR: hc r <page> <reg>\r\n");
			return;
		}
		int rc = radio_debug_bb_read(page, reg, &val);

		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
		} else {
			console_transport_printf("p%02X:0x%02X = %02X\r\n", page, reg, val);
		}
	} else if (strcmp(sub, "w") == 0) {
		if (!console_parse_hex8(arg1, &page) || !console_parse_hex8(arg2, &reg) ||
		    !console_parse_hex8(arg3, &val)) {
			console_transport_puts("ERR: hc w <page> <reg> <val>\r\n");
			return;
		}
		int rc = radio_debug_bb_write(page, reg, val);

		console_transport_printf("%s p%02X:0x%02X = %02X\r\n",
					 rc < 0 ? "ERR writing" : "OK", page, reg, val);
	} else {
		console_transport_puts("ERR: hc r|w ...\r\n");
	}
}

static void cmd_rssi(char *args)
{
	ARG_UNUSED(args);

	uint8_t noise, signal;
	int rc = radio_debug_rssi_raw(&noise, &signal);

	if (rc < 0) {
		console_transport_printf("ERR: %d\r\n", rc);
	} else {
		console_transport_printf("RSSI 0x1B: noise=%02X signal=%02X\r\n", noise, signal);
	}
}

/** "cp list" — print every known codeplug region: name, offset, size. */
static void cmd_cp_list(void)
{
	struct cp_region_info regions[20];
	int n = codeplug_list_regions(regions, ARRAY_SIZE(regions));

	for (int i = 0; i < n; i++) {
		console_transport_printf("%-24s off=0x%06lX size=0x%04lX\r\n", regions[i].name,
					 (unsigned long)regions[i].offset,
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

	if (!console_parse_hex32(arg1, &addr) || !console_parse_hex32(arg2, &len) || len == 0) {
		console_transport_puts("ERR: cp dump <addr> <len>\r\n");
		return;
	}

	while (len > 0) {
		size_t n = MIN((uint32_t)sizeof(buf), len);
		int rc = codeplug_raw_read(addr, buf, n);

		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
			return;
		}
		console_hexdump(addr, buf, n);
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
				console_transport_printf("ERR: %d\r\n", rc);
				return;
			}
			console_hexdump(off, buf, chunk);
			off += chunk;
			remaining -= chunk;
		}
		return;
	}
	console_transport_printf("ERR: unknown region '%s'\r\n", name);
}

/** "cp region <name> [idx]" — typed decode of one named region, or a raw dump. */
static void cmd_cp_region(char *args)
{
	char *name = strtok(args, " \t");
	char *idx_str = strtok(NULL, " \t");
	int idx = idx_str ? (int)strtoul(idx_str, NULL, 10) : 0;
	int rc;

	if (!name) {
		console_transport_puts("ERR: cp region <name> [idx]\r\n");
		return;
	}

	if (strcmp(name, "general-settings") == 0) {
		struct cp_general_settings s;

		rc = codeplug_get_general_settings(&s);
		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
			return;
		}
		console_transport_printf("radioName=%.8s radioId=%lu\r\n", s.radioName,
					 (unsigned long)s.radioId);
	} else if (strcmp(name, "device-info") == 0) {
		struct cp_device_info d;

		rc = codeplug_get_device_info(&d);
		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
			return;
		}
		console_transport_printf("model=%.8s sn=%.16s hw=%.8s fw=%.8s\r\n", d.model, d.sn,
					 d.hardwareVer, d.firmwareVer);
		console_transport_printf("uhf=%u-%uMHz vhf=%u-%uMHz\r\n", d.minUHFFreq,
					 d.maxUHFFreq, d.minVHFFreq, d.maxVHFFreq);
	} else if (strcmp(name, "channel") == 0) {
		struct cp_channel ch;

		rc = codeplug_get_channel(idx, &ch);
		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
			return;
		}
		if (!codeplug_channel_is_in_use(&ch)) {
			console_transport_puts("slot unused\r\n");
			return;
		}

		char rx_buf[12], tx_buf[12], lat_buf[16], lon_buf[16];
		uint8_t lat_raw[3] = {ch.locationLat0, ch.locationLat1, ch.locationLat2};
		uint8_t lon_raw[3] = {ch.locationLon0, ch.locationLon1, ch.locationLon2};

		console_transport_printf("name=%.16s rx=%lu tx=%lu mode=%u pwr=%u\r\n", ch.name,
					 (unsigned long)ch.rxFreq, (unsigned long)ch.txFreq,
					 ch.chMode, ch.power);
		console_transport_printf(
			"rxTone=%s txTone=%s\r\n",
			console_format_css(codeplug_decode_css(ch.rxTone), rx_buf, sizeof(rx_buf)),
			console_format_css(codeplug_decode_css(ch.txTone), tx_buf, sizeof(tx_buf)));
		console_transport_printf("lat=%s lon=%s\r\n",
					 console_format_latlon(codeplug_decode_latlon(lat_raw),
							       lat_buf, sizeof(lat_buf)),
					 console_format_latlon(codeplug_decode_latlon(lon_raw),
							       lon_buf, sizeof(lon_buf)));
	} else if (strcmp(name, "contact") == 0) {
		struct cp_contact c;

		rc = codeplug_get_contact(idx, &c);
		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
			return;
		}
		if (!codeplug_contact_is_in_use(&c)) {
			console_transport_puts("slot unused\r\n");
			return;
		}
		console_transport_printf("name=%.16s tg=%lu type=%u\r\n", c.name,
					 (unsigned long)c.tgNumber, c.callType);
	} else if (strcmp(name, "zone") == 0) {
		struct cp_zone z;
		int channels_per_zone;

		rc = codeplug_get_zone(idx, &z, &channels_per_zone);
		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
			return;
		}
		console_transport_printf("name=%.16s channels_per_zone=%d\r\n", z.name,
					 channels_per_zone);
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
		console_transport_printf("ERR: %d\r\n", rc);
		return;
	}
	console_transport_printf("magic=0x%04lX (latest known=0x%04X)\r\n", (unsigned long)magic,
				 CP_NV_SETTINGS_MAGIC_LATEST);

	if (magic == CP_NV_SETTINGS_MAGIC_LATEST) {
		struct cp_nv_settings *s = (struct cp_nv_settings *)buf;

		console_transport_printf("backlightMode=%u backLightTimeout=%u txPowerLevel=%u\r\n",
					 s->backlightMode, s->backLightTimeout, s->txPowerLevel);
	} else {
		console_transport_puts("magic mismatch -- raw dump:\r\n");
		console_hexdump(0, buf, MIN(sizeof(buf), (size_t)64));
	}
}

/** "cp info" — JEDEC ID, device info, and a calibration-marker sanity check. */
static void cmd_cp_info(void)
{
	uint8_t id[3];
	int rc = codeplug_get_jedec_id(id);

	if (rc < 0) {
		console_transport_printf("jedec: ERR %d\r\n", rc);
	} else {
		console_transport_printf("jedec: %02X %02X %02X\r\n", id[0], id[1], id[2]);
	}

	struct cp_device_info info;

	rc = codeplug_get_device_info(&info);
	if (rc < 0) {
		console_transport_printf("device-info: ERR %d\r\n", rc);
	} else {
		console_transport_printf("model=%.8s sn=%.16s fw=%.8s\r\n", info.model, info.sn,
					 info.firmwareVer);
	}

	static const uint8_t cal_marker[8] = {0x00, 0x25, 0x00, 0x40, 0x00, 0x45, 0x01, 0x40};
	struct cp_calibration cal;

	rc = codeplug_get_calibration(&cal);
	if (rc < 0) {
		console_transport_printf("calibration marker: ERR %d\r\n", rc);
	} else {
		bool ok = memcmp(cal.uhfCalFreqs[0], cal_marker, sizeof(cal_marker)) == 0;

		console_transport_printf("calibration marker: %s\r\n", ok ? "OK" : "MISMATCH");
	}
}

/** "cp dump|list|region|settings|info ..." — codeplug flash inspection. */
static void cmd_cp(char *args)
{
	char *sub = strtok(args, " \t");
	char *rest = strtok(NULL, "");

	if (!sub) {
		console_transport_puts("ERR: cp dump|list|region|settings|info ...\r\n");
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
		console_transport_puts("ERR: cp dump|list|region|settings|info ...\r\n");
	}
}

/** "rtc r" / "rtc w <year> <mon> <day> <hour> <min> <sec>" — read/set the hardware RTC. */
static void cmd_rtc(char *args)
{
	char *sub = strtok(args, " \t");

	if (!sub) {
		console_transport_puts("ERR: rtc r|w ...\r\n");
		return;
	}

	if (strcmp(sub, "r") == 0) {
		struct rtc_time tm;
		int rc = radio_debug_rtc_get(&tm);

		if (rc < 0) {
			console_transport_printf("ERR: %d\r\n", rc);
		} else {
			console_transport_printf("%04d-%02d-%02d %02d:%02d:%02d\r\n",
						 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
						 tm.tm_hour, tm.tm_min, tm.tm_sec);
		}
	} else if (strcmp(sub, "w") == 0) {
		int year, mon, day, hour, min, sec;

		if (!console_parse_dec(strtok(NULL, " \t"), &year) ||
		    !console_parse_dec(strtok(NULL, " \t"), &mon) ||
		    !console_parse_dec(strtok(NULL, " \t"), &day) ||
		    !console_parse_dec(strtok(NULL, " \t"), &hour) ||
		    !console_parse_dec(strtok(NULL, " \t"), &min) ||
		    !console_parse_dec(strtok(NULL, " \t"), &sec)) {
			console_transport_puts(
				"ERR: rtc w <year> <mon> <day> <hour> <min> <sec>\r\n");
			return;
		}

		int rc = radio_debug_rtc_set(year, mon, day, hour, min, sec);

		console_transport_printf("%s\r\n", rc < 0 ? "ERR writing" : "OK");
	} else {
		console_transport_puts("ERR: rtc r|w ...\r\n");
	}
}

static void cmd_reboot(char *args)
{
	ARG_UNUSED(args);

	console_transport_puts("rebooting...\r\n");
	/* Let the bytes above actually leave the FIFO before reset. */
	k_msleep(50);
	sys_reboot(SYS_REBOOT_WARM);
}

const struct console_cmd console_debug_cmds[] = {
	{"at", "r <reg> | w <reg> <hi> <lo>   raw AT1846S register read/write", cmd_at},
	{"hc", "r <page> <reg> | w <page> <reg> <val>   raw HR-C6000 register read/write", cmd_hc},
	{"rssi", "AT1846S 0x1B noise/signal", cmd_rssi},
	{"cp", "dump/list/region/settings/info ...   codeplug flash inspection", cmd_cp},
	{"rtc", "r | w <Y> <M> <D> <h> <m> <s>   read/set the hardware RTC", cmd_rtc},
	{"reboot", "warm-reset the MCU", cmd_reboot},
};

const size_t console_debug_cmd_count = ARRAY_SIZE(console_debug_cmds);
