// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "radio_debug.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>

#include <drivers/radio/radio_baseband.h>
#include <drivers/radio/radio_transceiver.h>

static const struct device *trx_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(at1846s));
}

static const struct device *bb_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(hr_c6000));
}

static const struct device *rtc_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(rtc));
}

int radio_debug_at_read(uint8_t reg, uint8_t *hi, uint8_t *lo)
{
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx_dev()->api;

	return api->read_raw(trx_dev(), reg, hi, lo);
}

int radio_debug_at_write(uint8_t reg, uint8_t hi, uint8_t lo)
{
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx_dev()->api;

	return api->write_raw(trx_dev(), reg, hi, lo);
}

int radio_debug_bb_read(uint8_t page, uint8_t reg, uint8_t *val)
{
	const struct radio_bb_api *api = (const struct radio_bb_api *)bb_dev()->api;

	return api->read_raw(bb_dev(), page, reg, val);
}

int radio_debug_bb_write(uint8_t page, uint8_t reg, uint8_t val)
{
	const struct radio_bb_api *api = (const struct radio_bb_api *)bb_dev()->api;

	return api->write_raw(bb_dev(), page, reg, val);
}

int radio_debug_rssi_raw(uint8_t *noise, uint8_t *signal)
{
	const struct radio_trx_api *api = (const struct radio_trx_api *)trx_dev()->api;

	return api->read_raw(trx_dev(), 0x1B, noise, signal);
}

int radio_debug_rtc_get(struct rtc_time *tm)
{
	return rtc_get_time(rtc_dev(), tm);
}

/** Sakamoto's algorithm -- day of week for a Gregorian date (0 = Sunday), matching struct rtc_time.
 * The STM32 RTC driver rejects tm_wday == -1 (unlike the "unknown" allowance in the struct's own
 * doc comment), so this can't be left unset on a set.
 */
static int day_of_week(int year, int mon, int day)
{
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	if (mon < 3) {
		year -= 1;
	}
	return (year + year / 4 - year / 100 + year / 400 + t[mon - 1] + day) % 7;
}

int radio_debug_rtc_set(int year, int mon, int day, int hour, int min, int sec)
{
	struct rtc_time tm = {
		.tm_sec = sec,
		.tm_min = min,
		.tm_hour = hour,
		.tm_mday = day,
		.tm_mon = mon - 1,
		.tm_year = year - 1900,
		.tm_wday = day_of_week(year, mon, day),
		.tm_yday = -1,
		.tm_isdst = -1,
		.tm_nsec = 0,
	};

	return rtc_set_time(rtc_dev(), &tm);
}
