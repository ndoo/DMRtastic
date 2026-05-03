// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/radio/radio_transceiver.h>

#include "usb_cdc.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define PMR446_CH1_HZ     446006250U  /* diagnostic channel */
#define PMR446_VOLUME_MIN 1U   /* REG_VOL low nibble; range 0..15 */
#define PMR446_SQUELCH_TH 55U  /* AT1846S 0x1B lo-byte noise threshold;
				* opens when noise < 55, closes when > 58. */

static int radio_tune_pmr446_ch1(void)
{
	const struct device *trx = DEVICE_DT_GET(DT_NODELABEL(at1846s));
	const struct radio_trx_api *api;
	int ret;

	if (!device_is_ready(trx)) {
		LOG_ERR("AT1846S not ready");
		return -ENODEV;
	}
	api = (const struct radio_trx_api *)trx->api;

	ret = api->set_mode(trx, RADIO_TRX_MODE_FM);
	if (ret < 0) {
		LOG_ERR("set_mode FM failed (%d)", ret);
		return ret;
	}
	ret = api->set_bandwidth(trx, RADIO_BW_12K5);
	if (ret < 0) {
		LOG_ERR("set_bandwidth 12.5k failed (%d)", ret);
		return ret;
	}
	ret = api->set_frequency(trx, PMR446_CH1_HZ, false);
	if (ret < 0) {
		LOG_ERR("set_frequency 446.00625 MHz failed (%d)", ret);
		return ret;
	}
	ret = api->set_squelch(trx, PMR446_SQUELCH_TH);
	if (ret < 0) {
		LOG_ERR("set_squelch failed (%d)", ret);
		return ret;
	}
	ret = api->enter_fm_rx(trx);
	if (ret < 0) {
		LOG_ERR("enter_fm_rx failed (%d)", ret);
		return ret;
	}
	ret = api->set_volume(trx, PMR446_VOLUME_MIN);
	if (ret < 0) {
		LOG_ERR("set_volume failed (%d)", ret);
		return ret;
	}

	LOG_INF("Tuned PMR446 ch1: 446.00625 MHz FM 12.5 kHz, vol 1/15, sq-th %u",
		PMR446_SQUELCH_TH);
	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("MAIN");

	ret = usb_cdc_init();
	if (ret != 0) {
		LOG_ERR("USB init failed (%d)", ret);
		return ret;
	}

	LOG_INF("Wait for DTR");
	usb_cdc_wait_dtr();
	LOG_INF("DTR set");

	usb_cdc_flush_and_enable();

	ret = radio_tune_pmr446_ch1();
	if (ret < 0) {
		LOG_ERR("radio tune-in failed (%d)", ret);
	}

	return 0;
}
