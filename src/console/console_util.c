// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "console_util.h"
#include "console_transport.h"

#include <stdio.h>
#include <stdlib.h>

#include <zephyr/sys/util.h>

bool console_parse_hex8(const char *s, uint8_t *out)
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

bool console_parse_hex32(const char *s, uint32_t *out)
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

bool console_parse_dec(const char *s, int *out)
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

void console_hexdump(uint32_t base_addr, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i += 16) {
		size_t n = MIN((size_t)16, len - i);

		console_transport_printf("%06lX:", (unsigned long)(base_addr + i));
		for (size_t j = 0; j < n; j++) {
			console_transport_printf(" %02X", data[i + j]);
		}
		console_transport_puts("\r\n");
	}
}

const char *console_format_css(struct cp_css css, char *buf, size_t buflen)
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

const char *console_format_latlon(int32_t v, char *buf, size_t buflen)
{
	bool neg = v < 0;
	int32_t abs_v = neg ? -v : v;

	snprintf(buf, buflen, "%s%ld.%04ld", neg ? "-" : "",
		 (long)(abs_v / 10000), (long)(abs_v % 10000));
	return buf;
}
