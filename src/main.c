// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/radio/radio_baseband.h>
#include <drivers/radio/radio_transceiver.h>

#include "model/battery.h"
#include "model/gps.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define PMR446_CH1_HZ     446006250U /* diagnostic channel */
#define PMR446_VOLUME_MIN 7U         /* RX volume percent (0–100); 7 % ≈ native level 1 */
#define PMR446_SQUELCH_TH                                                                          \
	55U /* AT1846S 0x1B lo-byte noise threshold;                                               \
	     * opens when noise < 55, closes when > 58.                                            \
	     */

/** Bring up the AT1846S/HR-C6000 pair and tune to PMR446 channel 1 FM RX. */
static int radio_tune_pmr446_ch1(void)
{
	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct device *bb = DEVICE_DT_GET(DT_NODELABEL(hr_c6000));
	const struct radio_trx_api *trx_api;
	const struct radio_bb_api *bb_api;
	int ret;

	if (!device_is_ready(trx)) {
		LOG_ERR("AT1846S not ready");
		return -ENODEV;
	}
	if (!device_is_ready(bb)) {
		LOG_ERR("HR-C6000 not ready");
		return -ENODEV;
	}
	trx_api = (const struct radio_trx_api *)trx->api;
	bb_api = (const struct radio_bb_api *)bb->api;

	ret = bb_api->set_fm_rx(bb);
	if (ret < 0) {
		LOG_ERR("HR-C6000 set_fm_rx failed (%d)", ret);
		return ret;
	}
	ret = bb_api->set_audio_path(bb, true);
	if (ret < 0) {
		LOG_ERR("HR-C6000 set_audio_path failed (%d)", ret);
		return ret;
	}

	ret = trx_api->set_mode(trx, RADIO_TRX_MODE_FM);
	if (ret < 0) {
		LOG_ERR("set_mode FM failed (%d)", ret);
		return ret;
	}
	ret = trx_api->set_bandwidth(trx, RADIO_BW_12K5);
	if (ret < 0) {
		LOG_ERR("set_bandwidth 12.5k failed (%d)", ret);
		return ret;
	}
	ret = trx_api->set_frequency(trx, PMR446_CH1_HZ, false);
	if (ret < 0) {
		LOG_ERR("set_frequency 446.00625 MHz failed (%d)", ret);
		return ret;
	}
	ret = trx_api->set_squelch(trx, PMR446_SQUELCH_TH);
	if (ret < 0) {
		LOG_ERR("set_squelch failed (%d)", ret);
		return ret;
	}
	ret = trx_api->enter_fm_rx(trx);
	if (ret < 0) {
		LOG_ERR("enter_fm_rx failed (%d)", ret);
		return ret;
	}
	ret = trx_api->set_volume(trx, PMR446_VOLUME_MIN);
	if (ret < 0) {
		LOG_ERR("set_volume failed (%d)", ret);
		return ret;
	}

	LOG_INF("Tuned PMR446 ch1: 446.00625 MHz FM 12.5 kHz, vol 1/15, sq-th %u",
		PMR446_SQUELCH_TH);
	return 0;
}

/** Configure led_green/led_red as outputs (see ui.c's "All LEDs" row). */
static int leds_init(void)
{
	static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
	static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
	int ret;

	if (!gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_red)) {
		LOG_ERR("LED GPIOs not ready");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	return gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
}

int main(void)
{
	int ret;

	LOG_INF("MAIN");

	/* Radio comes up first — other subsystems run in their own threads. */
	ret = radio_tune_pmr446_ch1();
	if (ret < 0) {
		LOG_ERR("radio tune-in failed (%d)", ret);
	}

	ret = leds_init();
	if (ret < 0) {
		LOG_ERR("LED init failed (%d)", ret);
	}

	ret = battery_init();
	if (ret < 0) {
		LOG_ERR("battery init failed (%d)", ret);
	}

	ret = gps_init();
	if (ret < 0) {
		LOG_ERR("GPS init failed (%d)", ret);
	}

	return 0;
}
