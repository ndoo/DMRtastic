// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "gps.h"

#include <ctype.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(gps, LOG_LEVEL_INF);

static const struct device *const s_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

/* Active-high GPS module power-enable line -- shares USART1's TX pin (PA9), which the module
 * never uses since it's RX-only (see mduv390plus.dts's usart1 node).
 */
static const struct gpio_dt_spec s_gps_en = GPIO_DT_SPEC_GET(DT_ALIAS(gps_en), gpios);

/* ---- RX: ring buffer fed by the UART ISR, drained non-blockingly by gps_poll() -- same
 * producer/consumer shape as console_transport.c's CDC-ACM RX, minus its blocking getc() side.
 */
#define GPS_RX_BUF_SIZE 256
RING_BUF_DECLARE(s_rx_ringbuf, GPS_RX_BUF_SIZE);

static void uart_irq_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t buf[16];
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n > 0) {
			ring_buf_put(&s_rx_ringbuf, buf, n);
		}
	}
}

int gps_init(void)
{
	int ret;

	if (!device_is_ready(s_uart)) {
		LOG_ERR("USART1 (GPS) not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&s_gps_en)) {
		LOG_ERR("GPS enable GPIO not ready");
		return -ENODEV;
	}

	/* No user-facing GPS on/off setting exists yet (Milestone 6a scope is the parser only);
	 * power the module unconditionally.
	 */
	ret = gpio_pin_configure_dt(&s_gps_en, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("GPS enable GPIO configure failed (%d)", ret);
		return ret;
	}

	uart_irq_callback_set(s_uart, uart_irq_cb);
	uart_irq_rx_enable(s_uart);
	return 0;
}

/* ---- NMEA sentence assembly + parsing -------------------------------------------- */

#define GPS_LINE_MAX 96 /* NMEA caps a sentence at 82 chars; leaves margin */

static char s_line[GPS_LINE_MAX];
static size_t s_line_len;

static enum gps_fix_state s_fix_state = GPS_FIX_NONE;
static int32_t s_lat_x10000, s_lon_x10000;
static uint16_t s_speed_knots_x10;
static uint8_t s_utc_hour, s_utc_min, s_utc_sec;
static bool s_time_valid;

/** Parses a plain decimal ([-]digits[.digits]) into fixed point scaled by 10^decimal_places;
 * digits past decimal_places are truncated, not rounded. False if s has no leading digit.
 */
static bool parse_fixed_decimal(const char *s, int decimal_places, int32_t *out)
{
	bool neg = false;
	int32_t whole = 0;
	int32_t frac = 0;
	int32_t scale = 1;

	if (*s == '-') {
		neg = true;
		s++;
	}
	if (!isdigit((unsigned char)*s)) {
		return false;
	}
	while (isdigit((unsigned char)*s)) {
		whole = whole * 10 + (*s - '0');
		s++;
	}

	for (int i = 0; i < decimal_places; i++) {
		scale *= 10;
	}

	if (*s == '.') {
		s++;
		int32_t place = scale;

		while (isdigit((unsigned char)*s)) {
			if (place > 1) {
				place /= 10;
				frac += (*s - '0') * place;
			}
			s++;
		}
	}

	*out = (whole * scale + frac) * (neg ? -1 : 1);
	return true;
}

/** Parses "DDMM.MMMM" (lat, degree_digits=2) / "DDDMM.MMMM" (lon, degree_digits=3) into
 * unsigned x1e4-degree fixed point; caller applies the hemisphere sign.
 */
static bool parse_lat_or_lon(const char *field, int degree_digits, int32_t *out_x10000)
{
	int32_t degrees = 0;
	int32_t minutes_x10000;

	for (int i = 0; i < degree_digits; i++) {
		if (!isdigit((unsigned char)field[i])) {
			return false;
		}
		degrees = degrees * 10 + (field[i] - '0');
	}
	if (!parse_fixed_decimal(field + degree_digits, 4, &minutes_x10000)) {
		return false;
	}

	*out_x10000 = degrees * 10000 + (minutes_x10000 + 30) / 60; /* minutes -> deg fraction */
	return true;
}

static bool parse_utc_time(const char *field, uint8_t *hour, uint8_t *min, uint8_t *sec)
{
	if (strlen(field) < 6) {
		return false;
	}
	for (int i = 0; i < 6; i++) {
		if (!isdigit((unsigned char)field[i])) {
			return false;
		}
	}
	*hour = (uint8_t)((field[0] - '0') * 10 + (field[1] - '0'));
	*min = (uint8_t)((field[2] - '0') * 10 + (field[3] - '0'));
	*sec = (uint8_t)((field[4] - '0') * 10 + (field[5] - '0'));
	return true;
}

/** Verifies the trailing "*hh" checksum (XOR of every byte between '$' and '*'); a sentence
 * with no '*' at all is accepted (some receivers truncate on transmit).
 */
static bool checksum_ok(const char *line, size_t len)
{
	const char *star = memchr(line, '*', len);
	uint8_t sum = 0;
	uint8_t hi, lo;
	int hi_val, lo_val;

	if (!star) {
		return true;
	}
	if ((size_t)(star - line) + 2 >= len) {
		return false; /* truncated checksum field */
	}
	for (const char *p = line + 1; p < star; p++) {
		sum ^= (uint8_t)*p;
	}

	hi = (uint8_t)star[1];
	lo = (uint8_t)star[2];
	if (!isxdigit(hi) || !isxdigit(lo)) {
		return false;
	}
	hi_val = isdigit(hi) ? hi - '0' : toupper(hi) - 'A' + 10;
	lo_val = isdigit(lo) ? lo - '0' : toupper(lo) - 'A' + 10;
	return sum == (uint8_t)((hi_val << 4) | lo_val);
}

static void parse_rmc(char *fields[], int count)
{
	if (count < 8) {
		return;
	}

	bool fix_valid = fields[2][0] == 'A';
	int32_t lat, lon, speed;

	s_fix_state = fix_valid ? GPS_FIX_ACQUIRED : GPS_FIX_SEARCHING;

	/* Time comes from the receiver's own clock and is populated regardless of fix status --
	 * unlike position/speed below, which are unreliable (and often stale/zero) while void.
	 */
	if (parse_utc_time(fields[1], &s_utc_hour, &s_utc_min, &s_utc_sec)) {
		s_time_valid = true;
	}
	if (!fix_valid) {
		return;
	}

	if (parse_lat_or_lon(fields[3], 2, &lat) && parse_lat_or_lon(fields[5], 3, &lon)) {
		s_lat_x10000 = (fields[4][0] == 'S') ? -lat : lat;
		s_lon_x10000 = (fields[6][0] == 'W') ? -lon : lon;
	}
	if (parse_fixed_decimal(fields[7], 1, &speed) && speed >= 0) {
		s_speed_knots_x10 = (uint16_t)speed;
	}
}

static void parse_gga(char *fields[], int count)
{
	if (count < 7) {
		return;
	}

	bool fix_valid = fields[6][0] != '0';
	int32_t lat, lon;

	if (s_fix_state != GPS_FIX_ACQUIRED) {
		s_fix_state = fix_valid ? GPS_FIX_ACQUIRED : GPS_FIX_SEARCHING;
	}
	if (parse_utc_time(fields[1], &s_utc_hour, &s_utc_min, &s_utc_sec)) {
		s_time_valid = true;
	}
	if (!fix_valid) {
		return;
	}

	if (parse_lat_or_lon(fields[2], 2, &lat) && parse_lat_or_lon(fields[4], 3, &lon)) {
		s_lat_x10000 = (fields[3][0] == 'S') ? -lat : lat;
		s_lon_x10000 = (fields[5][0] == 'W') ? -lon : lon;
	}
}

/** Splits line on literal ',' boundaries, preserving empty fields (NMEA sentences routinely omit
 * optional fields as back-to-back commas, e.g. GGA's differential-age/station-ID tail) -- unlike
 * strtok/strtok_r, which treat consecutive delimiters as one and would silently shift every field
 * after the first empty one. Truncates at '*' first (checksum already validated by the caller).
 */
static int split_fields(char *line, char *fields[], int max_fields)
{
	int count = 0;
	char *p = line;
	char *star = strchr(p, '*');

	if (star) {
		*star = '\0';
	}

	fields[count++] = p;
	while (count < max_fields) {
		char *comma = strchr(p, ',');

		if (!comma) {
			break;
		}
		*comma = '\0';
		p = comma + 1;
		fields[count++] = p;
	}
	return count;
}

/** line is NUL-terminated at len, starts with '$'. Mutated in place by split_fields(). */
static void parse_sentence(char *line, size_t len)
{
	if (!checksum_ok(line, len)) {
		return;
	}

	char *fields[16];
	int count = split_fields(line, fields, ARRAY_SIZE(fields));

	size_t id_len = strlen(fields[0]);

	if (id_len < 3) {
		return;
	}

	const char *type = fields[0] + id_len - 3; /* strip 2-letter talker ID (GP/GN/...) */

	if (strcmp(type, "RMC") == 0) {
		parse_rmc(fields, count);
	} else if (strcmp(type, "GGA") == 0) {
		parse_gga(fields, count);
	}
}

void gps_poll(void)
{
	uint8_t byte;

	while (ring_buf_get(&s_rx_ringbuf, &byte, 1) == 1) {
		if (byte == '$') {
			s_line_len = 0;
			s_line[s_line_len++] = (char)byte;
		} else if (byte == '\n') {
			if (s_line_len > 0) {
				s_line[s_line_len] = '\0';
				parse_sentence(s_line, s_line_len);
			}
			s_line_len = 0;
		} else if (byte == '\r') {
			/* skip */
		} else if (s_line_len > 0 && s_line_len < GPS_LINE_MAX - 1) {
			s_line[s_line_len++] = (char)byte;
		} else if (s_line_len >= GPS_LINE_MAX - 1) {
			s_line_len = 0; /* overflow: drop malformed line, resync on next '$' */
		}
	}
}

enum gps_fix_state gps_get_fix_state(void)
{
	return s_fix_state;
}

bool gps_get_position(int32_t *lat_x10000, int32_t *lon_x10000)
{
	if (s_fix_state != GPS_FIX_ACQUIRED) {
		return false;
	}
	*lat_x10000 = s_lat_x10000;
	*lon_x10000 = s_lon_x10000;
	return true;
}

uint16_t gps_get_speed_knots_tenths(void)
{
	return (s_fix_state == GPS_FIX_ACQUIRED) ? s_speed_knots_x10 : 0;
}

bool gps_get_utc_time(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
	if (!s_time_valid) {
		return false;
	}
	*hour = s_utc_hour;
	*min = s_utc_min;
	*sec = s_utc_sec;
	return true;
}
