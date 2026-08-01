// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/*
 * Text renderers over the same Controller/Model surface the LVGL screens use. No
 * <zephyr/device.h>, no DEVICE_DT_GET -- hardware access happens only inside the
 * controller/model calls below.
 */

#include "console_view.h"
#include "console_util.h"
#include "console_transport.h"

#include "controller/fm_vfo_controller.h"
#include "controller/channel_controller.h"
#include "controller/zone_controller.h"
#include "controller/settings_controller.h"
#include "model/radio_settings.h"

#include <string.h>

#include <zephyr/sys/util.h>

/** "status" — mirrors what screen_fm_vfo.c paints, as text: same fm_vfo_controller getters. */
static void cmd_status(char *args)
{
	ARG_UNUSED(args);

	uint32_t freq_hz = fm_vfo_controller_get_frequency_hz();

	console_transport_printf("vfo=%c freq=%lu.%05lu MHz rssi=%u squelch=%s\r\n",
				 fm_vfo_controller_get_current_vfo() == 0 ? 'A' : 'B',
				 (unsigned long)(freq_hz / 1000000U),
				 (unsigned long)(freq_hz % 1000000U), fm_vfo_controller_get_rssi(),
				 fm_vfo_controller_get_squelch_open() ? "open" : "closed");
}

/** "channel [next|prev]" — steps (applying to radio_state) then prints channel_controller's
 * current channel, same fields screen_fm_channel.c will eventually paint (Milestone 3b).
 */
static void cmd_channel(char *args)
{
	char *sub = strtok(args, " \t");

	if (sub && strcmp(sub, "next") == 0) {
		channel_controller_step(true);
	} else if (sub && strcmp(sub, "prev") == 0) {
		channel_controller_step(false);
	} else if (sub) {
		console_transport_puts("ERR: channel [next|prev]\r\n");
		return;
	}

	struct cp_channel ch;

	if (!channel_controller_get_current(&ch)) {
		console_transport_puts("no in-use channel\r\n");
		return;
	}

	char rx_css_buf[12], tx_css_buf[12];

	console_transport_printf(
		"ch=%d name=%.16s rx=%lu.%05lu MHz tx=%lu.%05lu MHz bw=%s pwr=%u\r\n",
		channel_controller_get_current_index(), ch.name,
		(unsigned long)(ch.rxFreq / 1000000U), (unsigned long)(ch.rxFreq % 1000000U),
		(unsigned long)(ch.txFreq / 1000000U), (unsigned long)(ch.txFreq % 1000000U),
		(ch.chFlag4 & CP_CHANNEL_FLAG4_BW_25K) ? "25K" : "12.5K", ch.power);
	console_transport_printf(
		"rxTone=%s txTone=%s\r\n",
		console_format_css(codeplug_decode_css(ch.rxTone), rx_css_buf, sizeof(rx_css_buf)),
		console_format_css(codeplug_decode_css(ch.txTone), tx_css_buf, sizeof(tx_css_buf)));
}

/** "zone [next|prev]" — steps (no channel/radio interaction yet, Milestone 4b) then prints
 * zone_controller's current zone, same fields the future Zones screen (Milestone 4c) will
 * eventually paint.
 */
static void cmd_zone(char *args)
{
	char *sub = strtok(args, " \t");

	if (sub && strcmp(sub, "next") == 0) {
		zone_controller_step(true);
	} else if (sub && strcmp(sub, "prev") == 0) {
		zone_controller_step(false);
	} else if (sub) {
		console_transport_puts("ERR: zone [next|prev]\r\n");
		return;
	}

	struct cp_zone z;
	int channels_per_zone;

	if (!zone_controller_get_current(&z, &channels_per_zone)) {
		console_transport_puts("no in-use zone\r\n");
		return;
	}

	console_transport_printf("slot=%d count=%d name=%.16s channels_per_zone=%d\r\n",
				 zone_controller_get_current_slot(), zone_controller_get_count(),
				 z.name, channels_per_zone);
}

/** Prints one menu_item_t table with a "group.Label = value" line per row. */
static void print_items(const char *group, const menu_item_t *items, uint8_t count)
{
	for (uint8_t i = 0; i < count; i++) {
		console_transport_printf("%s.%s = %s\r\n", group, items[i].label,
					 items[i].value_str ? items[i].value_str : "(action)");
	}
}

static void cmd_settings_list(void)
{
	const menu_item_t *radio_items, *display_items;
	uint8_t radio_count, display_count;

	settings_controller_get_radio_items(&radio_items, &radio_count);
	settings_controller_get_display_items(&display_items, &display_count);

	print_items("radio", radio_items, radio_count);
	print_items("display", display_items, display_count);
}

/** Finds label (case-sensitive, exact) in items[0..count); returns NULL if not found. */
static const menu_item_t *find_item(const menu_item_t *items, uint8_t count, const char *label)
{
	for (uint8_t i = 0; i < count; i++) {
		if (strcmp(items[i].label, label) == 0) {
			return &items[i];
		}
	}
	return NULL;
}

/** "settings set <radio|display> <label> up|down" — calls the matching row's on_select(),
 * the same callback screen_settings.c's row click/encoder-turn invokes.
 */
static void cmd_settings_set(char *args)
{
	char *group = strtok(args, " \t");
	char *label = strtok(NULL, " \t");
	char *dir_str = strtok(NULL, " \t");

	if (!group || !label || !dir_str ||
	    (strcmp(dir_str, "up") != 0 && strcmp(dir_str, "down") != 0)) {
		console_transport_puts("ERR: settings set radio|display <label> up|down\r\n");
		return;
	}

	const menu_item_t *items;
	uint8_t count;

	if (strcmp(group, "radio") == 0) {
		settings_controller_get_radio_items(&items, &count);
	} else if (strcmp(group, "display") == 0) {
		settings_controller_get_display_items(&items, &count);
	} else {
		console_transport_puts("ERR: settings set radio|display <label> up|down\r\n");
		return;
	}

	const menu_item_t *item = find_item(items, count, label);

	if (!item) {
		console_transport_printf("ERR: unknown %s setting '%s'\r\n", group, label);
		return;
	}

	item->on_select(strcmp(dir_str, "up") == 0 ? 1 : -1);
	console_transport_printf("%s.%s = %s\r\n", group, item->label,
				 item->value_str ? item->value_str : "(action)");
}

/** "settings list" / "settings set ..." */
static void cmd_settings(char *args)
{
	char *sub = strtok(args, " \t");
	char *rest = strtok(NULL, "");

	if (!sub) {
		console_transport_puts(
			"ERR: settings list | set radio|display <label> up|down\r\n");
		return;
	}

	if (strcmp(sub, "list") == 0) {
		cmd_settings_list();
	} else if (strcmp(sub, "set") == 0) {
		cmd_settings_set(rest ? rest : "");
	} else {
		console_transport_puts(
			"ERR: settings list | set radio|display <label> up|down\r\n");
	}
}

/** "css tx|rx off" / "css tx|rx ctcss <tenths_hz>" / "css tx|rx dcs <code> n|i" — sets the
 * shared CTCSS/DCS setting via settings_set_css(); settings_controller.c's already-registered
 * on_settings_changed() subscriber applies it to hardware and refreshes the Settings screen's
 * value_str, so this stays in sync with the LVGL UI instead of silently diverging from it.
 * (There's only one CSS setting, not independent TX/RX -- see settings_get_css()'s doc comment
 * -- so "tx"/"rx" here just describes which side the caller cares about, both set the same value.)
 */
static void cmd_css(char *args)
{
	char *side = strtok(args, " \t");
	char *sub = strtok(NULL, " \t");
	char *arg1 = strtok(NULL, " \t");
	char *arg2 = strtok(NULL, " \t");

	if (!side || (strcmp(side, "tx") != 0 && strcmp(side, "rx") != 0)) {
		console_transport_puts("ERR: css tx|rx off|ctcss <tenths_hz>|dcs <code> n|i\r\n");
		return;
	}

	struct cp_css css = {.type = CP_CSS_NONE, .value = 0, .inverted = false};

	if (!sub || strcmp(sub, "off") == 0) {
		/* css already CP_CSS_NONE. */
	} else if (strcmp(sub, "ctcss") == 0) {
		int tenths;

		if (!console_parse_dec(arg1, &tenths) || tenths <= 0) {
			console_transport_puts("ERR: css tx|rx ctcss <tenths_hz>\r\n");
			return;
		}
		css.type = CP_CSS_CTCSS;
		css.value = (uint16_t)tenths;
	} else if (strcmp(sub, "dcs") == 0) {
		int code;

		if (!console_parse_dec(arg1, &code) || !arg2 ||
		    (arg2[0] != 'n' && arg2[0] != 'i')) {
			console_transport_puts("ERR: css tx|rx dcs <code> n|i\r\n");
			return;
		}
		css.type = CP_CSS_DCS;
		css.value = (uint16_t)code;
		css.inverted = (arg2[0] == 'i');
	} else {
		console_transport_puts("ERR: css tx|rx off|ctcss <tenths_hz>|dcs <code> n|i\r\n");
		return;
	}

	settings_set_css(css);

	char buf[16];

	console_transport_printf("%s: OK\r\n", console_format_css(css, buf, sizeof(buf)));
}

const struct console_cmd console_view_cmds[] = {
	{"status", "current VFO/frequency/RSSI/squelch", cmd_status},
	{"channel", "[next|prev] -- current/step FM channel", cmd_channel},
	{"zone", "[next|prev] -- current/step zone", cmd_zone},
	{"settings", "list | set radio|display <label> up|down", cmd_settings},
	{"css", "tx|rx off|ctcss <tenths_hz>|dcs <code> n|i", cmd_css},
};

const size_t console_view_cmd_count = ARRAY_SIZE(console_view_cmds);
