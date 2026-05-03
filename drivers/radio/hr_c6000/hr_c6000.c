/*
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 * SPDX-License-Identifier: MIT
 *
 * Hongrui HR-C6000 DMR/FM baseband modem driver.
 *
 * Implements only what FM RX needs (power-on, init burst, FM RX codec
 * routing, audio feedthrough); DMR/TX paths are stubbed with -ENOTSUP.
 */

#define DT_DRV_COMPAT hongrui_hr_c6000

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/radio/radio_baseband.h>

#include "hr_c6000_regs.h"

LOG_MODULE_REGISTER(hr_c6000, CONFIG_RADIO_HR_C6000_LOG_LEVEL);

#define HRC6000_PWR_SETTLE_MS 10

struct hr_c6000_config {
	struct spi_dt_spec  spi_ctrl;
	struct gpio_dt_spec power_down_gpio;
	struct gpio_dt_spec timeslot_int_gpio;
	struct gpio_dt_spec sys_int_gpio;
	struct gpio_dt_spec rftx_int_gpio;
	uint32_t            if_frequency_hz;
	bool                rf_recv_iq_mode;
	bool                swap_iq_adc;
	bool                swap_iq_dac;
};

struct hr_c6000_data {
	struct k_mutex bus_lock;
	bool           powered;
};

/* ---- Control-bus SPI helpers ----------------------------------------- */

static int hr_c6000_ctrl_write(const struct device *dev,
			       uint8_t page, uint8_t reg, uint8_t val)
{
	const struct hr_c6000_config *cfg = dev->config;
	uint8_t tx[3] = { page, reg, val };
	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_transceive_dt(&cfg->spi_ctrl, &tx_set, NULL);
}

/** Read one register: transmits {page|0x80, reg, dummy}, returns the third RX byte. */
static int hr_c6000_ctrl_read(const struct device *dev,
			      uint8_t page, uint8_t reg, uint8_t *val)
{
	const struct hr_c6000_config *cfg = dev->config;
	uint8_t tx[3] = { (uint8_t)(page | 0x80), reg, 0xFF };
	uint8_t rx[3] = { 0 };
	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
	int rc = spi_transceive_dt(&cfg->spi_ctrl, &tx_set, &rx_set);

	if (rc < 0) {
		return rc;
	}
	*val = rx[2];
	return 0;
}

static int hr_c6000_ctrl_write_pairs(const struct device *dev, uint8_t page,
				     const struct hr_c6000_reg_pair *pairs,
				     size_t count)
{
	int rc;

	for (size_t i = 0; i < count; i++) {
		rc = hr_c6000_ctrl_write(dev, page, pairs[i].reg, pairs[i].val);
		if (rc < 0) {
			return rc;
		}
	}
	return 0;
}

/** Burst-write a contiguous register range starting at reg; the chip auto-increments. */
static int hr_c6000_ctrl_write_array(const struct device *dev,
				     uint8_t page, uint8_t reg,
				     const uint8_t *values, size_t length)
{
	const struct hr_c6000_config *cfg = dev->config;
	uint8_t header[2] = { page, reg };
	const struct spi_buf tx_bufs[2] = {
		{ .buf = header, .len = sizeof(header) },
		{ .buf = (void *)values, .len = length },
	};
	const struct spi_buf_set tx_set = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};

	return spi_transceive_dt(&cfg->spi_ctrl, &tx_set, NULL);
}

/* ---- Power control ---------------------------------------------------- */

static int hr_c6000_power_on(const struct device *dev)
{
	const struct hr_c6000_config *cfg = dev->config;
	struct hr_c6000_data *data = dev->data;
	int rc;

	if (data->powered) {
		return 0;
	}
	rc = gpio_pin_set_dt(&cfg->power_down_gpio, 1);
	if (rc < 0) {
		return rc;
	}
	k_msleep(HRC6000_PWR_SETTLE_MS);
	data->powered = true;
	return 0;
}

static int hr_c6000_power_off(const struct device *dev)
{
	const struct hr_c6000_config *cfg = dev->config;
	struct hr_c6000_data *data = dev->data;
	int rc;

	if (!data->powered) {
		return 0;
	}
	rc = gpio_pin_set_dt(&cfg->power_down_gpio, 0);
	if (rc < 0) {
		return rc;
	}
	data->powered = false;
	return 0;
}

/* ---- Init register burst ---------------------------------------------- */

/** Run the full control-bus register burst that brings the chip up for FM RX. */
static int hr_c6000_run_init_sequence(const struct device *dev)
{
	const struct hr_c6000_config *cfg = dev->config;
	int rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_pll_regs,
		ARRAY_SIZE(hr_c6000_init_pll_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_setup_regs,
		ARRAY_SIZE(hr_c6000_init_setup_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_array(dev, HRC6000_PAGE_CTRL, 0x11,
		hr_c6000_init_ctrl_0x11_array,
		sizeof(hr_c6000_init_ctrl_0x11_array));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_vocoder_regs,
		ARRAY_SIZE(hr_c6000_init_vocoder_regs));
	if (rc < 0) return rc;

	/* AMBE silence prefill (page 0x03) skipped: no audio-bus driver
	 * wired yet, and FM RX doesn't touch page 0x03 memory. */

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_post_flush_regs,
		ARRAY_SIZE(hr_c6000_init_post_flush_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_array(dev, HRC6000_PAGE_AUX, 0x10,
		hr_c6000_init_aux_0x10_array,
		sizeof(hr_c6000_init_aux_0x10_array));
	if (rc < 0) return rc;
	rc = hr_c6000_ctrl_write_array(dev, HRC6000_PAGE_AUX, 0x30,
		hr_c6000_init_aux_0x30_array,
		sizeof(hr_c6000_init_aux_0x30_array));
	if (rc < 0) return rc;
	rc = hr_c6000_ctrl_write_array(dev, HRC6000_PAGE_AUX, 0x40,
		hr_c6000_init_aux_0x40_array,
		sizeof(hr_c6000_init_aux_0x40_array));
	if (rc < 0) return rc;
	rc = hr_c6000_ctrl_write_array(dev, HRC6000_PAGE_AUX, 0x50,
		hr_c6000_init_aux_0x50_array,
		sizeof(hr_c6000_init_aux_0x50_array));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_AUX,
		hr_c6000_init_aux_more_regs,
		ARRAY_SIZE(hr_c6000_init_aux_more_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_late_regs,
		ARRAY_SIZE(hr_c6000_init_late_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_AUX,
		hr_c6000_init_aux_cleanup_regs,
		ARRAY_SIZE(hr_c6000_init_aux_cleanup_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_rxprep_regs,
		ARRAY_SIZE(hr_c6000_init_rxprep_regs));
	if (rc < 0) return rc;

	rc = hr_c6000_ctrl_write_array(dev, HRC6000_PAGE_AUX, 0x04,
		hr_c6000_ms_sync_pattern,
		sizeof(hr_c6000_ms_sync_pattern));
	if (rc < 0) return rc;

	/* Override the IF tuning word (regs 0x07/0x08/0x09) only if DT
	 * differs from the 450 kHz default hr_c6000_init_setup_regs already wrote. */
	if (cfg->if_frequency_hz != 450000) {
		uint32_t ifw = cfg->if_frequency_hz;
		uint8_t hi  = (ifw >> 16) & 0xFF;
		uint8_t mid = (ifw >>  8) & 0xFF;
		uint8_t lo  =  ifw        & 0xFF;
		rc = hr_c6000_ctrl_write(dev, HRC6000_PAGE_CTRL, 0x07, hi);
		if (rc < 0) return rc;
		rc = hr_c6000_ctrl_write(dev, HRC6000_PAGE_CTRL, 0x08, mid);
		if (rc < 0) return rc;
		rc = hr_c6000_ctrl_write(dev, HRC6000_PAGE_CTRL, 0x09, lo);
		if (rc < 0) return rc;
	}

	return hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
		hr_c6000_init_final_regs,
		ARRAY_SIZE(hr_c6000_init_final_regs));
}

/* ---- API: set_fm_rx --------------------------------------------------- */

static int hr_c6000_api_set_fm_rx(const struct device *dev)
{
	struct hr_c6000_data *data = dev->data;
	int rc;

	if (!data->powered) {
		return -ENODEV;
	}
	k_mutex_lock(&data->bus_lock, K_FOREVER);
	rc = hr_c6000_ctrl_write_pairs(dev, HRC6000_PAGE_CTRL,
				       hr_c6000_set_fm_rx_regs,
				       ARRAY_SIZE(hr_c6000_set_fm_rx_regs));
	k_mutex_unlock(&data->bus_lock);
	return rc;
}

/* ---- API: set_audio_path --------------------------------------------- */

static int hr_c6000_api_set_audio_path(const struct device *dev,
				       bool fm_feedthrough)
{
	struct hr_c6000_data *data = dev->data;
	int rc;

	if (!data->powered) {
		return -ENODEV;
	}
	k_mutex_lock(&data->bus_lock, K_FOREVER);
	rc = hr_c6000_ctrl_write(dev, HRC6000_PAGE_CTRL,
				 HRC6000_REG_36_FM_FEED,
				 fm_feedthrough ? HRC6000_36_FM_FEED_ON
						: HRC6000_36_FM_FEED_OFF);
	k_mutex_unlock(&data->bus_lock);
	return rc;
}

/* ---- API: power_down -------------------------------------------------- */

/** Power the chip down, or power up and re-run the init sequence. */
static int hr_c6000_api_power_down(const struct device *dev, bool down)
{
	if (down) {
		return hr_c6000_power_off(dev);
	}
	int rc = hr_c6000_power_on(dev);
	if (rc < 0) {
		return rc;
	}
	return hr_c6000_run_init_sequence(dev);
}

/* ---- Stubs for the rest of radio_bb_api ------------------------------ */

static int hr_c6000_api_unsupported(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -ENOTSUP;
}

static int hr_c6000_api_set_fm_tx(const struct device *dev,
				  const struct radio_bb_fm_tx_cfg *cfg)
{
	ARG_UNUSED(dev); ARG_UNUSED(cfg);
	return -ENOTSUP;
}

static int hr_c6000_api_set_dmr(const struct device *dev,
				const struct radio_bb_dmr_cfg *cfg)
{
	ARG_UNUSED(dev); ARG_UNUSED(cfg);
	return -ENOTSUP;
}

static int hr_c6000_api_set_mic(const struct device *dev, bool enable)
{
	ARG_UNUSED(dev); ARG_UNUSED(enable);
	return -ENOTSUP;
}

static int hr_c6000_api_mute_audio(const struct device *dev, bool mute)
{
	ARG_UNUSED(dev); ARG_UNUSED(mute);
	return -ENOTSUP;
}

static int hr_c6000_api_send_tone(const struct device *dev, uint32_t freq_hz)
{
	ARG_UNUSED(dev); ARG_UNUSED(freq_hz);
	return -ENOTSUP;
}

static int hr_c6000_api_set_dtmf(const struct device *dev, uint8_t code)
{
	ARG_UNUSED(dev); ARG_UNUSED(code);
	return -ENOTSUP;
}

static int hr_c6000_api_dtmf_off(const struct device *dev, bool enable_mic)
{
	ARG_UNUSED(dev); ARG_UNUSED(enable_mic);
	return -ENOTSUP;
}

static int hr_c6000_api_get_rx_ids(const struct device *dev,
				   uint32_t *tg_id, uint32_t *src_id)
{
	ARG_UNUSED(dev);
	if (tg_id)  *tg_id  = 0;
	if (src_id) *src_id = 0;
	return -ENOTSUP;
}

static int hr_c6000_api_get_rx_color_code(const struct device *dev,
					  uint8_t *cc)
{
	ARG_UNUSED(dev);
	if (cc) *cc = 0;
	return -ENOTSUP;
}

static int hr_c6000_api_clear_rx_ids(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int hr_c6000_api_reset_sync(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int hr_c6000_api_set_dmr_ids(const struct device *dev,
				    uint32_t tg_id, uint32_t src_id)
{
	ARG_UNUSED(dev); ARG_UNUSED(tg_id); ARG_UNUSED(src_id);
	return -ENOTSUP;
}

static int hr_c6000_api_set_talker_alias(const struct device *dev,
					 const char *text)
{
	ARG_UNUSED(dev); ARG_UNUSED(text);
	return -ENOTSUP;
}

static int hr_c6000_api_set_talker_alias_location(const struct device *dev,
						  int32_t lat_1e7,
						  int32_t lon_1e7)
{
	ARG_UNUSED(dev); ARG_UNUSED(lat_1e7); ARG_UNUSED(lon_1e7);
	return -ENOTSUP;
}

static int hr_c6000_api_set_rx_gain(const struct device *dev, int8_t gain)
{
	ARG_UNUSED(dev); ARG_UNUSED(gain);
	return -ENOTSUP;
}

static int hr_c6000_api_set_agc_gain(const struct device *dev, int8_t gain)
{
	ARG_UNUSED(dev); ARG_UNUSED(gain);
	return -ENOTSUP;
}

static int hr_c6000_api_inject_ambe_frame(const struct device *dev,
					  const uint8_t *frame)
{
	ARG_UNUSED(dev); ARG_UNUSED(frame);
	return -ENOTSUP;
}

static int hr_c6000_api_flush_audio(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -ENOTSUP;
}

static int hr_c6000_api_get_sync_state(const struct device *dev,
				       bool *has_sync, bool *cc_held,
				       enum radio_bb_waking_mode *waking)
{
	ARG_UNUSED(dev);
	if (has_sync) *has_sync = false;
	if (cc_held)  *cc_held  = false;
	if (waking)   *waking   = RADIO_BB_WAKING_NONE;
	return 0;
}

static int hr_c6000_api_register_callbacks(const struct device *dev,
					   const struct radio_bb_callbacks *cbs)
{
	ARG_UNUSED(dev); ARG_UNUSED(cbs);
	/* Phase 3.5 has no DMR ISRs; callbacks are accepted but never fire. */
	return 0;
}

static int hr_c6000_api_write_raw(const struct device *dev,
				  uint8_t page, uint8_t reg, uint8_t val)
{
	return hr_c6000_ctrl_write(dev, page, reg, val);
}

static int hr_c6000_api_read_raw(const struct device *dev,
				 uint8_t page, uint8_t reg, uint8_t *val)
{
	return hr_c6000_ctrl_read(dev, page, reg, val);
}

static const struct radio_bb_api hr_c6000_api = {
	.set_fm_rx                 = hr_c6000_api_set_fm_rx,
	.set_fm_tx                 = hr_c6000_api_set_fm_tx,
	.set_dmr                   = hr_c6000_api_set_dmr,
	.init_digital              = hr_c6000_api_unsupported,
	.terminate_digital         = hr_c6000_api_unsupported,
	.set_audio_path            = hr_c6000_api_set_audio_path,
	.set_mic                   = hr_c6000_api_set_mic,
	.mute_audio                = hr_c6000_api_mute_audio,
	.send_tone                 = hr_c6000_api_send_tone,
	.set_dtmf                  = hr_c6000_api_set_dtmf,
	.dtmf_off                  = hr_c6000_api_dtmf_off,
	.get_rx_ids                = hr_c6000_api_get_rx_ids,
	.get_rx_color_code         = hr_c6000_api_get_rx_color_code,
	.clear_rx_ids              = hr_c6000_api_clear_rx_ids,
	.reset_sync                = hr_c6000_api_reset_sync,
	.set_dmr_ids               = hr_c6000_api_set_dmr_ids,
	.set_talker_alias          = hr_c6000_api_set_talker_alias,
	.set_talker_alias_location = hr_c6000_api_set_talker_alias_location,
	.set_rx_gain               = hr_c6000_api_set_rx_gain,
	.set_agc_gain              = hr_c6000_api_set_agc_gain,
	.inject_ambe_frame         = hr_c6000_api_inject_ambe_frame,
	.flush_audio               = hr_c6000_api_flush_audio,
	.get_sync_state            = hr_c6000_api_get_sync_state,
	.register_callbacks        = hr_c6000_api_register_callbacks,
	.power_down                = hr_c6000_api_power_down,
	.write_raw                 = hr_c6000_api_write_raw,
	.read_raw                  = hr_c6000_api_read_raw,
};

/* ---- Init ------------------------------------------------------------- */

/** Configure GPIOs, power on the chip, and run its init register sequence. */
static int hr_c6000_init(const struct device *dev)
{
	const struct hr_c6000_config *cfg = dev->config;
	struct hr_c6000_data *data = dev->data;
	int rc;

	k_mutex_init(&data->bus_lock);
	data->powered = false;

	if (!spi_is_ready_dt(&cfg->spi_ctrl)) {
		LOG_ERR("control SPI not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->power_down_gpio)) {
		LOG_ERR("power-down gpio not ready");
		return -ENODEV;
	}

	/* Hold the chip in reset/power-down at boot; consumers bring it up. */
	rc = gpio_pin_configure_dt(&cfg->power_down_gpio, GPIO_OUTPUT_INACTIVE);
	if (rc < 0) {
		LOG_ERR("power-down gpio configure failed (%d)", rc);
		return rc;
	}

	/* Configure the interrupt-output GPIOs as inputs. Their edge type
	 * gets enabled by the DMR ISR phase; for now they sit idle. */
	if (cfg->timeslot_int_gpio.port != NULL) {
		(void)gpio_pin_configure_dt(&cfg->timeslot_int_gpio, GPIO_INPUT);
	}
	if (cfg->sys_int_gpio.port != NULL) {
		(void)gpio_pin_configure_dt(&cfg->sys_int_gpio, GPIO_INPUT);
	}
	if (cfg->rftx_int_gpio.port != NULL) {
		(void)gpio_pin_configure_dt(&cfg->rftx_int_gpio, GPIO_INPUT);
	}

	rc = hr_c6000_power_on(dev);
	if (rc < 0) {
		LOG_ERR("power-on failed (%d)", rc);
		return rc;
	}

	rc = hr_c6000_run_init_sequence(dev);
	if (rc < 0) {
		LOG_ERR("init sequence failed (%d)", rc);
		return rc;
	}

	/* Diagnostic readback of the IF-tuning bytes to confirm the SPI
	 * control bus reaches the chip; expect 0x0B 0xB8 0x00 (450 kHz). */
	uint8_t r07 = 0xAA, r08 = 0xAA, r09 = 0xAA;
	(void)hr_c6000_ctrl_read(dev, HRC6000_PAGE_CTRL, 0x07, &r07);
	(void)hr_c6000_ctrl_read(dev, HRC6000_PAGE_CTRL, 0x08, &r08);
	(void)hr_c6000_ctrl_read(dev, HRC6000_PAGE_CTRL, 0x09, &r09);
	LOG_INF("HR-C6000 init OK; IF readback 0x%02X 0x%02X 0x%02X",
		r07, r08, r09);
	return 0;
}

/* ---- DT instantiation ------------------------------------------------- */

#define HRC6000_INIT(inst)								\
	static const struct hr_c6000_config hr_c6000_cfg_##inst = {			\
		.spi_ctrl = SPI_DT_SPEC_INST_GET(inst,					\
			SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |				\
			SPI_MODE_CPOL | SPI_MODE_CPHA |					\
			SPI_WORD_SET(8), 0),						\
		.power_down_gpio   = GPIO_DT_SPEC_INST_GET(inst, power_down_gpios),	\
		.timeslot_int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, timeslot_int_gpios, {0}),\
		.sys_int_gpio      = GPIO_DT_SPEC_INST_GET_OR(inst, sys_int_gpios, {0}),\
		.rftx_int_gpio     = GPIO_DT_SPEC_INST_GET_OR(inst, rftx_int_gpios, {0}),\
		.if_frequency_hz   = DT_INST_PROP_OR(inst, if_frequency_hz, 450000),	\
		.swap_iq_adc       = DT_INST_PROP(inst, swap_iq_adc),			\
		.swap_iq_dac       = DT_INST_PROP(inst, swap_iq_dac),			\
		.rf_recv_iq_mode   =							\
			!strcmp(DT_INST_PROP_OR(inst, rf_recv_mode, "if"), "iq"),	\
	};										\
	static struct hr_c6000_data hr_c6000_data_##inst;				\
	DEVICE_DT_INST_DEFINE(inst, hr_c6000_init, NULL,				\
		&hr_c6000_data_##inst, &hr_c6000_cfg_##inst,				\
		POST_KERNEL, CONFIG_RADIO_HR_C6000_INIT_PRIORITY,			\
		&hr_c6000_api);

DT_INST_FOREACH_STATUS_OKAY(HRC6000_INIT)
