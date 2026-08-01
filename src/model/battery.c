// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "battery.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#define BATTERY_NODE DT_NODELABEL(battery)

/* voltage-divider is descriptive DT metadata only; the ADC API doesn't apply it. */
#define BATTERY_OUTPUT_OHMS DT_PROP(BATTERY_NODE, output_ohms)
#define BATTERY_FULL_OHMS   DT_PROP(BATTERY_NODE, full_ohms)

#define BATTERY_CELL_COUNT 2

/* Typical single-cell LiPo rest-voltage curve, 10% steps. */
static const uint16_t ocv_table_mv[11] = {
	3450, 3680, 3740, 3770, 3790, 3820, 3870, 3920, 3980, 4060, 4200,
	/*  0%    10%   20%   30%   40%   50%   60%   70%   80%   90%  100% */
};

#define BATTERY_SAMPLE_PERIOD_MS 1000

/* Settle delay: divider's ~7 kOhm Thevenin R against a worst-case 10uF sense cap gives tau~=70ms;
 * 5*tau before the first sample after channel setup.
 */
#define BATTERY_SETTLE_MS 400

static const struct adc_dt_spec s_adc = ADC_DT_SPEC_GET(BATTERY_NODE);

static int64_t s_next_sample_uptime_ms;
static int32_t s_last_mv = -1; /* -1 sentinel: no successful reading yet */

static uint8_t ocv_lookup_percent(uint16_t cell_mv)
{
	if (cell_mv <= ocv_table_mv[0]) {
		return 0;
	}
	if (cell_mv >= ocv_table_mv[10]) {
		return 100;
	}

	for (int i = 1; i <= 10; i++) {
		if (cell_mv <= ocv_table_mv[i]) {
			uint16_t lo = ocv_table_mv[i - 1];
			uint16_t hi = ocv_table_mv[i];

			return (uint8_t)((i - 1) * 10 + (cell_mv - lo) * 10 / (hi - lo));
		}
	}

	return 100; /* unreachable */
}

static int battery_sample_once(int32_t *out_mv)
{
	uint16_t raw = 0;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int32_t mv;
	int ret = adc_sequence_init_dt(&s_adc, &sequence);

	if (ret < 0) {
		return ret;
	}
	ret = adc_read_dt(&s_adc, &sequence);
	if (ret < 0) {
		return ret;
	}

	mv = raw;
	ret = adc_raw_to_millivolts_dt(&s_adc, &mv); /* adc-pin mV */
	if (ret < 0) {
		return ret;
	}

	*out_mv = mv * BATTERY_FULL_OHMS / BATTERY_OUTPUT_OHMS; /* pack mV */
	return 0;
}

int battery_init(void)
{
	int32_t mv;
	int ret;

	if (!adc_is_ready_dt(&s_adc)) {
		LOG_ERR("battery ADC device not ready");
		return -ENODEV;
	}
	ret = adc_channel_setup_dt(&s_adc);
	if (ret < 0) {
		LOG_ERR("battery adc_channel_setup_dt failed (%d)", ret);
		return ret;
	}

	k_msleep(BATTERY_SETTLE_MS);

	ret = battery_sample_once(&mv);
	if (ret < 0) {
		LOG_ERR("battery initial sample failed (%d)", ret);
		return ret;
	}
	s_last_mv = mv;
	s_next_sample_uptime_ms = k_uptime_get() + BATTERY_SAMPLE_PERIOD_MS;

	LOG_INF("battery init: %d mV", mv);
	return 0;
}

void battery_poll(void)
{
	int64_t now;
	int32_t mv;
	int ret;

	if (s_last_mv < 0) {
		return; /* battery_init() never succeeded */
	}
	now = k_uptime_get();
	if (now < s_next_sample_uptime_ms) {
		return;
	}
	s_next_sample_uptime_ms = now + BATTERY_SAMPLE_PERIOD_MS;

	ret = battery_sample_once(&mv);
	if (ret < 0) {
		LOG_WRN("battery sample failed (%d)", ret);
		return;
	}
	s_last_mv = mv;
}

uint16_t battery_get_millivolts(void)
{
	return (s_last_mv < 0) ? 0 : (uint16_t)s_last_mv;
}

uint8_t battery_get_percent(void)
{
	if (s_last_mv < 0) {
		return 0;
	}
	return ocv_lookup_percent((uint16_t)(s_last_mv / BATTERY_CELL_COUNT));
}
