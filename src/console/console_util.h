/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_CONSOLE_UTIL_H_
#define DMRTASTIC_CONSOLE_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model/codeplug.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared arg-parsing/formatting helpers used by both console_debug.c and
 * console_view.c command handlers.
 */

/** Parse a bare hex byte (no 0x prefix); returns false on any malformed input. */
bool console_parse_hex8(const char *s, uint8_t *out);

/** Parse a bare hex value (no 0x prefix, up to 32 bits); false on malformed input. */
bool console_parse_hex32(const char *s, uint32_t *out);

/** Parse a bare decimal int; false on malformed input. */
bool console_parse_dec(const char *s, int *out);

/** Print len bytes starting at display address base_addr, 16 bytes/line. */
void console_hexdump(uint32_t base_addr, const uint8_t *data, size_t len);

/** Format a decoded CSS value, e.g. "203.5Hz" / "D023N" / "D023I" / "off". */
const char *console_format_css(struct cp_css css, char *buf, size_t buflen);

/** Format a signed x1e4-degree fixed-point value as e.g. "-123.4567", no floating point. */
const char *console_format_latlon(int32_t v, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CONSOLE_UTIL_H_ */
