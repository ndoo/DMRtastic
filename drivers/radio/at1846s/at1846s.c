// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/*
 * Auctus AT1846S RF transceiver driver.
 *
 * I2C at 400 kHz; reads use two transactions since a repeated start
 * returns stale data. Implements the project's radio_trx_api table.
 */

#define DT_DRV_COMPAT auctus_at1846s

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/radio/radio_transceiver.h>

#include "at1846s_regs.h"

LOG_MODULE_REGISTER(at1846s, CONFIG_RADIO_AT1846S_LOG_LEVEL);

/* ---- Driver data / config -------------------------------------------- */

#if DT_ANY_INST_HAS_PROP_STATUS_OKAY(band_select_gpios)
#define AT1846S_HAS_BAND_GPIOS 1
#else
#define AT1846S_HAS_BAND_GPIOS 0
#endif

/* Fixed size so the table-init macro emits the same shape for any DT instance. */
#define AT1846S_BAND_GPIO_MAX 4

/* Software squelch hysteresis: close threshold = open threshold + 3 units. */
#define AT1846S_SQ_HYSTERESIS 3U

/* Consecutive squelch-poll cycles a matched CTCSS/DCS tone is allowed to go undetected
 * before the squelch thread re-mutes on tone loss, once already open -- avoids audio
 * chattering on a single marginal detector reading mid-transmission. Opening still
 * requires an immediate match; this only debounces losing it.
 */
#define AT1846S_CSS_HOLD_POLLS 4U

struct at1846s_config {
	struct i2c_dt_spec i2c;

	/* Flat array of (low_hz, high_hz) pairs; length = freq_range_count * 2 */
	const uint32_t *freq_ranges;
	uint8_t freq_range_count;

#if AT1846S_HAS_BAND_GPIOS
	struct gpio_dt_spec band_gpios[AT1846S_BAND_GPIO_MAX];
	uint8_t band_gpio_count;
#endif

	struct gpio_dt_spec rx_audio_mux_gpio;
	struct gpio_dt_spec amp_en_gpio;
	struct gpio_dt_spec spk_mute_gpio;
	struct gpio_dt_spec squelch_gpio;
	struct gpio_dt_spec css_gpio;

	bool baseband_handles_deemph;
	bool baseband_handles_css;
	bool squelch_hardware;

	int32_t tx_freq_offset_hz;
};

struct at1846s_reg_cache_slot {
	bool valid[2];
	uint8_t hi[2];
	uint8_t lo[2];
};

struct at1846s_data {
	struct k_mutex i2c_lock;
	uint8_t current_bank;

#ifdef CONFIG_RADIO_AT1846S_REGISTER_CACHE
	struct at1846s_reg_cache_slot cache[128];
#endif

	radio_trx_squelch_fn_t squelch_handler;
	radio_trx_css_fn_t css_handler;

	uint32_t current_rx_freq_hz;
	uint8_t current_band_index;

	/* Software squelch state (unused in hardware squelch mode) */
	uint8_t sq_threshold; /* noise byte threshold; open when noise < this */
	bool sq_open;         /* current squelch state */
	bool rx_active;       /* true while in FM/DMR RX */
	struct k_sem sq_wake; /* enter_fm_rx gives this to kick the thread */

	/* RX CTCSS/DCS tone-gating (set by set_rx_ctcss/set_rx_dcs, consumed by the
	 * squelch thread -- carrier squelch alone never accounts for tone match).
	 */
	bool rx_css_active;              /* an RX tone/code is currently armed */
	enum radio_css_type rx_css_type; /* which check_css() type to test */
	uint8_t css_miss_count;          /* consecutive polls since the tone was last seen */
};

/* ---- Low-level I2C helpers -------------------------------------------- */

/** Write a register, tracking bank selects and (if enabled) caching to skip redundant writes. */
static int at1846s_write_reg_locked(const struct device *dev, uint8_t reg, uint8_t hi, uint8_t lo)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	uint8_t buf[3] = {reg, hi, lo};
	int rc;

	if (reg == AT1846S_REG_BANK_SEL) {
		data->current_bank = lo & 0x01;
	}

#ifdef CONFIG_RADIO_AT1846S_REGISTER_CACHE
	if (reg < ARRAY_SIZE(data->cache) && reg != AT1846S_REG_BANK_SEL) {
		struct at1846s_reg_cache_slot *slot = &data->cache[reg];
		uint8_t bank = data->current_bank;

		if (slot->valid[bank] && slot->hi[bank] == hi && slot->lo[bank] == lo) {
			return 0;
		}
	}
#endif

	rc = i2c_write_dt(&cfg->i2c, buf, sizeof(buf));
	if (rc < 0) {
		LOG_ERR("write reg 0x%02X failed (%d)", reg, rc);
		return rc;
	}

#ifdef CONFIG_RADIO_AT1846S_REGISTER_CACHE
	if (reg < ARRAY_SIZE(data->cache) && reg != AT1846S_REG_BANK_SEL) {
		struct at1846s_reg_cache_slot *slot = &data->cache[reg];
		uint8_t bank = data->current_bank;

		slot->hi[bank] = hi;
		slot->lo[bank] = lo;
		slot->valid[bank] = true;
	}
#endif

	return 0;
}

static int at1846s_write_reg(const struct device *dev, uint8_t reg, uint8_t hi, uint8_t lo)
{
	struct at1846s_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	rc = at1846s_write_reg_locked(dev, reg, hi, lo);
	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

static int at1846s_read_reg_locked(const struct device *dev, uint8_t reg, uint8_t *hi, uint8_t *lo)
{
	const struct at1846s_config *cfg = dev->config;
	uint8_t out = reg;
	uint8_t in[2] = {0};
	int rc;

	rc = i2c_write_dt(&cfg->i2c, &out, 1);
	if (rc < 0) {
		LOG_ERR("read addr 0x%02X failed (%d)", reg, rc);
		return rc;
	}
	rc = i2c_read_dt(&cfg->i2c, in, sizeof(in));
	if (rc < 0) {
		LOG_ERR("read data 0x%02X failed (%d)", reg, rc);
		return rc;
	}
	*hi = in[0];
	*lo = in[1];
	return 0;
}

static int at1846s_read_reg(const struct device *dev, uint8_t reg, uint8_t *hi, uint8_t *lo)
{
	struct at1846s_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	rc = at1846s_read_reg_locked(dev, reg, hi, lo);
	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

static int at1846s_modify_reg_locked(const struct device *dev, uint8_t reg, uint16_t mask_keep,
				     uint16_t value_set)
{
	uint8_t hi, lo;
	int rc;

	rc = at1846s_read_reg_locked(dev, reg, &hi, &lo);
	if (rc < 0) {
		return rc;
	}
	uint16_t v = ((uint16_t)hi << 8) | lo;
	v = (v & mask_keep) | value_set;
	return at1846s_write_reg_locked(dev, reg, (v >> 8) & 0xFF, v & 0xFF);
}

/** Apply a bulk register table; a AT1846S_REG_DELAY entry sleeps instead of writing. */
static int at1846s_write_table(const struct device *dev, const struct at1846s_reg_entry *entries,
			       size_t count)
{
	struct at1846s_data *data = dev->data;
	int rc = 0;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	for (size_t i = 0; i < count; i++) {
		const struct at1846s_reg_entry *e = &entries[i];

		if (e->reg == AT1846S_REG_DELAY) {
			k_msleep(((uint32_t)e->hi << 8) | e->lo);
			continue;
		}
		rc = at1846s_write_reg_locked(dev, e->reg, e->hi, e->lo);
		if (rc < 0) {
			break;
		}
	}
	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

static void at1846s_invalidate_cache(struct at1846s_data *data)
{
#ifdef CONFIG_RADIO_AT1846S_REGISTER_CACHE
	memset(data->cache, 0, sizeof(data->cache));
#else
	ARG_UNUSED(data);
#endif
}

/* ---- Band selection --------------------------------------------------- */

static int at1846s_band_index_for(const struct at1846s_config *cfg, uint32_t freq_hz)
{
	for (uint8_t i = 0; i < cfg->freq_range_count; i++) {
		uint32_t lo = cfg->freq_ranges[i * 2];
		uint32_t hi = cfg->freq_ranges[i * 2 + 1];

		if (freq_hz >= lo && freq_hz <= hi) {
			return i;
		}
	}
	return -1;
}

static int at1846s_apply_band_gpios(const struct at1846s_config *cfg, uint8_t band_index)
{
#if AT1846S_HAS_BAND_GPIOS
	for (uint8_t i = 0; i < cfg->band_gpio_count; i++) {
		int rc = gpio_pin_set_dt(&cfg->band_gpios[i], (i == band_index) ? 1 : 0);

		if (rc < 0) {
			return rc;
		}
	}
#else
	ARG_UNUSED(cfg);
	ARG_UNUSED(band_index);
#endif
	return 0;
}

/* ---- CSS detection-select re-arm (mode/bandwidth tables clobber it) --- */

/* set_rx_ctcss()/set_rx_dcs() select the tone-detect source with a *masked* modify of
 * REG_VOICE_SEL (0x3A, mask 0xFFE0). But at1846s_mode_fm_regs[]/at1846s_mode_dmr_regs[]/
 * the bandwidth tables all write 0x3A *wholesale* (no mask, e.g. `{ 0x3A, 0x00, 0xC3 }`),
 * silently discarding that selection. This driver applies RX CSS from a separate
 * settings-change callback (Milestone 2), decoupled from mode/bandwidth changes, so
 * nothing guarantees the CSS-select write lands after a mode/bandwidth change -- re-apply
 * the selection explicitly after both instead.
 */
static int at1846s_reapply_rx_css_select(const struct device *dev)
{
	struct at1846s_data *data = dev->data;
	uint16_t value_set;
	int rc;

	if (!data->rx_css_active) {
		return 0;
	}

	switch (data->rx_css_type) {
	case RADIO_CSS_DCS_NORM:
		value_set = 0x0002;
		break;
	case RADIO_CSS_DCS_INV:
		value_set = 0x0004;
		break;
	case RADIO_CSS_CTCSS:
	default:
		value_set = 0x0008;
		break;
	}

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	rc = at1846s_modify_reg_locked(dev, AT1846S_REG_VOICE_SEL, 0xFFE0, value_set);
	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

/* ---- API: set_frequency ---------------------------------------------- */

/** Select band GPIOs and program the RX/TX frequency, holding RX off across the change. */
static int at1846s_api_set_frequency(const struct device *dev, uint32_t freq_hz, bool tx)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	int rc;

	int band = at1846s_band_index_for(cfg, freq_hz);
	if (band < 0) {
		LOG_ERR("freq %u Hz out of supported ranges", freq_hz);
		return -ERANGE;
	}

	uint32_t programmed_hz = freq_hz;
	if (tx && cfg->tx_freq_offset_hz != 0) {
		programmed_hz = (uint32_t)((int64_t)freq_hz + cfg->tx_freq_offset_hz);
	}

	/*
	 * AT1846S frequency register: f_reg = freq_hz / 62.5.
	 * Integer math: freq_hz * 2 / 125, split across 0x29 (hi) / 0x2A (lo).
	 */
	uint64_t scaled = (uint64_t)programmed_hz * 2U / 125U;
	uint8_t fh_h = (scaled >> 24) & 0xFF;
	uint8_t fh_l = (scaled >> 16) & 0xFF;
	uint8_t fl_h = (scaled >> 8) & 0xFF;
	uint8_t fl_l = scaled & 0xFF;

	rc = at1846s_apply_band_gpios(cfg, (uint8_t)band);
	if (rc < 0) {
		LOG_ERR("band gpio update failed (%d)", rc);
		return rc;
	}
	data->current_band_index = (uint8_t)band;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);

	/* Hold RX off across the frequency program to avoid a glitch. */
	rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CONTROL, 0xFFD9, 0x0006);
	if (rc < 0) {
		goto out;
	}

	rc = at1846s_write_reg_locked(dev, AT1846S_REG_FREQ_MODE, 0x87, 0x63);
	if (rc < 0) {
		goto out;
	}
	rc = at1846s_write_reg_locked(dev, AT1846S_REG_FREQ_HI, fh_h, fh_l);
	if (rc < 0) {
		goto out;
	}
	rc = at1846s_write_reg_locked(dev, AT1846S_REG_FREQ_LO, fl_h, fl_l);
	if (rc < 0) {
		goto out;
	}
	rc = at1846s_write_reg_locked(dev, AT1846S_REG_SQ_THRESH, 0x0C, 0x15);
	if (rc < 0) {
		goto out;
	}

	/* Re-arm RX. */
	rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CONTROL, 0xFFD9, 0x0026);

out:
	k_mutex_unlock(&data->i2c_lock);
	if (rc == 0 && !tx) {
		data->current_rx_freq_hz = freq_hz;
	}
	return rc;
}

static int at1846s_api_get_frequency(const struct device *dev, uint32_t *freq_hz)
{
	struct at1846s_data *data = dev->data;

	if (!freq_hz) {
		return -EINVAL;
	}
	*freq_hz = data->current_rx_freq_hz;
	return 0;
}

/* ---- API: set_mode / set_bandwidth ----------------------------------- */

static int at1846s_api_set_mode(const struct device *dev, enum radio_trx_mode mode)
{
	const struct at1846s_reg_entry *table;
	size_t count;

	if (mode == RADIO_TRX_MODE_FM) {
		table = at1846s_mode_fm_regs;
		count = ARRAY_SIZE(at1846s_mode_fm_regs);
	} else {
		table = at1846s_mode_dmr_regs;
		count = ARRAY_SIZE(at1846s_mode_dmr_regs);
	}
	int rc = at1846s_write_table(dev, table, count);
	if (rc < 0) {
		return rc;
	}
	return at1846s_reapply_rx_css_select(dev);
}

/** Apply the bandwidth register table, then toggle RX off/on to latch it. */
static int at1846s_api_set_bandwidth(const struct device *dev, enum radio_bw bw)
{
	const struct at1846s_reg_entry *table;
	size_t count;
	uint8_t ctrl_hi;

	if (bw == RADIO_BW_25K) {
		table = at1846s_bw_25k_regs;
		count = ARRAY_SIZE(at1846s_bw_25k_regs);
		ctrl_hi = AT1846S_CTRL_HI_BW_25K;
	} else {
		table = at1846s_bw_12k5_regs;
		count = ARRAY_SIZE(at1846s_bw_12k5_regs);
		ctrl_hi = AT1846S_CTRL_HI_BW_12K5;
	}

	int rc = at1846s_write_table(dev, table, count);
	if (rc < 0) {
		return rc;
	}

	struct at1846s_data *data = dev->data;
	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	/* Set bandwidth bits in REG_CONTROL high byte; force RX off then on. */
	rc = at1846s_write_reg_locked(dev, AT1846S_REG_CONTROL, ctrl_hi, AT1846S_CTRL_LO_IDLE);
	if (rc == 0) {
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_CONTROL, ctrl_hi,
					      AT1846S_CTRL_LO_RX_ON);
	}
	k_mutex_unlock(&data->i2c_lock);
	if (rc < 0) {
		return rc;
	}
	return at1846s_reapply_rx_css_select(dev);
}

/* ---- API: get_rssi ---------------------------------------------------- */

static int at1846s_api_get_rssi(const struct device *dev, uint8_t *signal, uint8_t *noise)
{
	uint8_t hi, lo;
	int rc = at1846s_read_reg(dev, AT1846S_REG_RSSI, &hi, &lo);
	if (rc < 0) {
		return rc;
	}
	if (signal) {
		*signal = hi;
	}
	if (noise) {
		*noise = lo;
	}
	return 0;
}

/* ---- Speaker amp helpers --------------------------------------------- */

static void at1846s_set_speaker(const struct at1846s_config *cfg, bool on)
{
	if (cfg->amp_en_gpio.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->amp_en_gpio, on ? 1 : 0);
	}
	if (cfg->spk_mute_gpio.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->spk_mute_gpio, on ? 0 : 1);
	}
}

/* ---- API: set_volume / set_squelch ----------------------------------- */

/** Map a 0-100% volume to the chip's 4-bit range, mirrored into both nibbles of REG_VOL. */
static int at1846s_api_set_volume(const struct device *dev, uint8_t pct)
{
	if (pct > 100) {
		pct = 100;
	}
	/* Map 0–100 % to native 4-bit range 0–15 (rounded). */
	uint8_t level = (uint8_t)((pct * 15U + 50U) / 100U);
	/* REG_VOL low nibble = volume_2 (RX volume 0..15); mirror the value into volume_1
	 * (high-byte low nibble) so both digital volume gates open together.
	 */
	uint8_t hi = level & 0x0F;
	uint8_t lo = (uint8_t)((level << 4) | level);
	return at1846s_write_reg(dev, AT1846S_REG_VOL, hi, lo);
}

/** Set squelch threshold; in software mode this only stores it, the squelch thread mutes. */
static int at1846s_api_set_squelch(const struct device *dev, uint8_t level)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;

	if (!cfg->squelch_hardware) {
		/* Store the threshold for the polling squelch thread's own RSSI-based
		 * decision; leave SQ_THRESH itself untouched. The user's squelch level
		 * doesn't need to drive SQ_THRESH at all -- set_frequency() here already
		 * writes a single fixed threshold pair (0x0C/0x15) on every retune, and
		 * nothing else needs to touch it, since the actual mute decision is
		 * entirely RSSI-based, not sq_cmp-based. A previous version of this
		 * function explicitly zeroed the register afterwards under the assumption
		 * that "monitor mode" was needed to stop the chip gating audio on its own,
		 * but this driver's software squelch already controls muting independently
		 * via the speaker GPIOs (see at1846s_squelch_thread_fn()) -- SQ_THRESH
		 * being armed doesn't unmute anything by itself. Zeroing it instead broke
		 * RX CTCSS/DCS: the tone decoder's flag bits never assert without a
		 * genuinely armed threshold.
		 */
		data->sq_threshold = level;
		return 0;
	}

	uint8_t open_th = level;
	uint8_t shut_th = (uint8_t)(level + 9);
	return at1846s_write_reg(dev, AT1846S_REG_SQ_THRESH, open_th, shut_th);
}

/* ---- API: voice channel + tone1 -------------------------------------- */

/** Route the voice mux to the given channel and enable its LPF bypass where needed. */
static int at1846s_api_set_voice_channel(const struct device *dev, enum radio_voice_ch ch,
					 uint8_t gain, uint16_t deviation)
{
	struct at1846s_data *data = dev->data;
	int rc;

	ARG_UNUSED(gain);
	ARG_UNUSED(deviation);

	k_mutex_lock(&data->i2c_lock, K_FOREVER);

	switch (ch) {
	case RADIO_VOICE_CH_TONE1:
	case RADIO_VOICE_CH_TONE2:
	case RADIO_VOICE_CH_DTMF:
		rc = at1846s_modify_reg_locked(dev, AT1846S_REG_TONE_MODE, 0xFFFF, 0xC000);
		if (rc < 0) {
			goto out;
		}
		rc = at1846s_modify_reg_locked(dev, AT1846S_REG_BYPASS_LPF, 0xFFFE, 0x0001);
		if (rc < 0) {
			goto out;
		}
		break;
	default:
		rc = at1846s_modify_reg_locked(dev, AT1846S_REG_BYPASS_LPF, 0xFFFE, 0x0000);
		if (rc < 0) {
			goto out;
		}
		break;
	}

	/*
	 * Voice-channel select lives in bits 14:12 of register 0x3A.
	 * Mask 0x8FFF preserves bit 15 and bits 7:0.
	 */
	rc = at1846s_modify_reg_locked(dev, AT1846S_REG_VOICE_SEL, 0x8FFF, ((uint16_t)ch) << 8);
out:
	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

static int at1846s_api_set_tone1_freq(const struct device *dev, uint16_t tone_freq_val)
{
	return at1846s_write_reg(dev, AT1846S_REG_TONE1, (tone_freq_val >> 8) & 0xFF,
				 tone_freq_val & 0xFF);
}

/* ---- API: CSS (CTCSS / DCS) ------------------------------------------ */

/** Program (or clear, if tone_dHz is 0) the TX CTCSS tone; no-op if the baseband handles CSS. */
static int at1846s_api_set_tx_ctcss(const struct device *dev, uint16_t tone_dHz)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	int rc;

	if (cfg->baseband_handles_css) {
		return 0;
	}

	k_mutex_lock(&data->i2c_lock, K_FOREVER);

	if (tone_dHz == 0) {
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS1, 0, 0);
		if (rc == 0) {
			rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS2, 0, 0);
		}
		if (rc == 0) {
			rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CSS_EN, 0xF9FF, 0x0000);
		}
	} else {
		/* Register wants ctcss_freq(Hz)*100 (programming guide sec. 7); tone_dHz is
		 * ctcss_freq(Hz)*10, so scale up by 10 -- max CTCSS tone (~254.1 Hz) is well
		 * under the uint16_t ceiling after scaling.
		 */
		uint16_t reg_word = (uint16_t)(tone_dHz * 10);

		rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS1, (reg_word >> 8) & 0xFF,
					      reg_word & 0xFF);
		if (rc == 0) {
			rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS2, 0, 0);
		}
		if (rc == 0) {
			rc = at1846s_write_reg_locked(dev, AT1846S_REG_DCS_HI, 0, 0);
		}
		if (rc == 0) {
			rc = at1846s_write_reg_locked(dev, AT1846S_REG_DCS_LO, 0, 0);
		}
		if (rc == 0) {
			rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CSS_EN, 0xF9FF, 0x0600);
		}
	}

	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

/** Program (or clear) the RX CTCSS tone and its detect threshold; no-op if baseband handles CSS. */
static int at1846s_api_set_rx_ctcss(const struct device *dev, uint16_t tone_dHz)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	int threshold;
	int rc;

	if (cfg->baseband_handles_css) {
		return 0;
	}

	if (tone_dHz == 0) {
		k_mutex_lock(&data->i2c_lock, K_FOREVER);
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS1, 0, 0);
		if (rc == 0) {
			rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS2, 0, 0);
		}
		if (rc == 0) {
			rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CSS_EN, 0xF9FF, 0x0000);
		}
		k_mutex_unlock(&data->i2c_lock);
		if (rc == 0) {
			data->rx_css_active = false;
			data->css_miss_count = 0;
		}
		return rc;
	}

	/* Same *10 scaling as the TX path -- see comment there. The detect threshold is
	 * derived from this already-scaled register value (Hz*100), not the raw tenths-Hz
	 * tone_dHz. Using tone_dHz directly here (as a prior fix pass assumed was safe)
	 * programs a threshold roughly 10x too loose/tight, which let the wrong squelch
	 * decision through regardless of actual tone match.
	 */
	uint16_t reg_word = (uint16_t)(tone_dHz * 10);

	threshold = (25000 - reg_word) / 1000;
	if (reg_word > 24000) {
		threshold = 1;
	}

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS1, 0, 0);
	if (rc == 0) {
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_DCS_HI, 0, 0);
	}
	if (rc == 0) {
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_DCS_LO, 0, 0);
	}
	if (rc == 0) {
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS2, (reg_word >> 8) & 0xFF,
					      reg_word & 0xFF);
	}
	if (rc == 0) {
		rc = at1846s_write_reg_locked(dev, AT1846S_REG_CTCSS_TH, threshold & 0xFF,
					      threshold & 0xFF);
	}
	if (rc == 0) {
		rc = at1846s_modify_reg_locked(dev, AT1846S_REG_VOICE_SEL, 0xFFE0, 0x0008);
	}
	k_mutex_unlock(&data->i2c_lock);
	if (rc == 0) {
		data->rx_css_active = true;
		data->rx_css_type = RADIO_CSS_CTCSS;
		data->css_miss_count = 0;
	}
	return rc;
}

static int at1846s_api_set_tx_dcs(const struct device *dev, uint16_t code, bool inv)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(code);
	ARG_UNUSED(inv);
	/* DCS encoding is deferred; CTCSS covers the bring-up path. */
	return -ENOTSUP;
}

static int at1846s_api_set_rx_dcs(const struct device *dev, uint16_t code, bool inv)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(code);
	ARG_UNUSED(inv);
	return -ENOTSUP;
}

/** Read the CSS detect flags register and report whether the given CTCSS/DCS type matched.
 *
 * A prior investigation pass found that datasheet-position `hi & 0x01` (bit9/ctcss2_cmp
 * per the programming guide's sec. 17 "Flag" bit map) never asserted on real hardware
 * even against a genuinely matching tone, and that `lo & 0x01` (sq_cmp) tracked
 * *something* correlated with carrier presence -- but a same-carrier mismatched-tone
 * control proved that bit isn't tone-specific at all: squelch opened identically at
 * 103.5 Hz (matching), 100.0 Hz, and 67.0 Hz (both should have been rejected). Tracing
 * this driver's own register-write order against the datasheet's bit-flag semantics,
 * rather than more bit-polling, found three compounding root causes:
 *
 * 1. Correct tone-match detection requires *both* `FlagsL & 0x01` (sq_cmp) *and*, for
 *    CTCSS, `FlagsH & 0x01` (ctcss2_cmp) -- an AND of carrier-hardware-compare and
 *    tone-match-compare, not either bit alone. This driver's `lo & 0x01`-only check was
 *    reading sq_cmp (real, but tone-independent) while never consulting ctcss2_cmp at
 *    all -- consistent with "opens on any carrier, regardless of tone."
 * 2. ctcss2_cmp's datasheet position (`hi & 0x01`) never asserting was itself a real
 *    symptom, not a wrong-bit guess: `at1846s_api_set_mode()`/`_set_bandwidth()` write
 *    REG_VOICE_SEL (0x3A) *wholesale* as part of their mode/bandwidth tables, silently
 *    discarding the masked CTCSS2-select bits `set_rx_ctcss()` wrote there earlier. RX
 *    CSS is applied from a separate, later settings-change callback here, so nothing
 *    enforced select-after-mode/bandwidth ordering -- fixed by
 *    `at1846s_reapply_rx_css_select()`, called at the end of both functions.
 * 3. sq_cmp itself was structurally near-guaranteed clear: `set_squelch()`'s software
 *    branch used to zero SQ_THRESH (0x49) to force "monitor mode", when the register
 *    only ever needs a fixed real threshold (0x0C/0x15) regardless of squelch level,
 *    since the actual mute decision is entirely RSSI-based, not sq_cmp-based -- fixed
 *    by leaving SQ_THRESH alone in `set_squelch()` (see that function's comment).
 *
 * With SQ_THRESH genuinely armed and the CTCSS2 select bits no longer getting clobbered,
 * both bits this function reads now match their datasheet positions -- no swap needed.
 * DCS bits stay unverified on hardware (set_tx_dcs/set_rx_dcs are still -ENOTSUP stubs,
 * so the DCS branch below is unreachable) but are written per the datasheet's DCS
 * behavior: cdcss1_cmp/cdcss2_cmp live in the *low* byte alongside sq_cmp (bits 7/6),
 * not the high byte, and DCS detection never consults the high byte at all.
 */
static int at1846s_api_check_css(const struct device *dev, uint16_t tone, enum radio_css_type type,
				 bool *detected)
{
	uint8_t hi, lo;
	int rc;

	ARG_UNUSED(tone);

	rc = at1846s_read_reg(dev, AT1846S_REG_CSS_FLAGS, &hi, &lo);
	if (rc < 0) {
		return rc;
	}

	switch (type) {
	case RADIO_CSS_DCS_NORM:
		*detected = (lo & 0x81) == 0x81; /* sq_cmp + cdcss1_cmp */
		break;
	case RADIO_CSS_DCS_INV:
		*detected = (lo & 0x41) == 0x41; /* sq_cmp + cdcss2_cmp */
		break;
	case RADIO_CSS_CTCSS:
	default:
		*detected = ((lo & 0x01) != 0) && ((hi & 0x01) != 0); /* sq_cmp + ctcss2_cmp */
		break;
	}
	return 0;
}

/* ---- API: callbacks --------------------------------------------------- */

static int at1846s_api_register_squelch_cb(const struct device *dev, radio_trx_squelch_fn_t fn)
{
	struct at1846s_data *data = dev->data;
	data->squelch_handler = fn;
	return 0;
}

static int at1846s_api_register_css_cb(const struct device *dev, radio_trx_css_fn_t fn)
{
	struct at1846s_data *data = dev->data;
	data->css_handler = fn;
	return 0;
}

/* ---- API: enter_fm_rx / standby -------------------------------------- */

/** Switch to FM mode, enable the RX audio mux, and start the squelch thread if in software mode. */
static int at1846s_api_enter_fm_rx(const struct device *dev)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	int rc;

	rc = at1846s_api_set_mode(dev, RADIO_TRX_MODE_FM);
	if (rc < 0) {
		return rc;
	}

	if (cfg->rx_audio_mux_gpio.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->rx_audio_mux_gpio, 1);
	}

	if (cfg->squelch_hardware) {
		/* Chip gates audio internally; unmute immediately. */
		at1846s_set_speaker(cfg, true);
	} else {
		/* Software squelch: start muted; thread unmutes on signal. */
		at1846s_set_speaker(cfg, false);
		data->sq_open = false;
		data->css_miss_count = 0;
		data->rx_active = true;
	}

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CONTROL, 0xFFD9, AT1846S_CTRL_LO_RX_ON);
	k_mutex_unlock(&data->i2c_lock);

	if (!cfg->squelch_hardware) {
		/* Kick the squelch thread after chip is in RX. */
		k_sem_give(&data->sq_wake);
	}

	return rc;
}

static int at1846s_api_standby(const struct device *dev)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	int rc;

	data->rx_active = false;
	at1846s_set_speaker(cfg, false);

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	rc = at1846s_modify_reg_locked(dev, AT1846S_REG_CONTROL, 0xFFD9, AT1846S_CTRL_LO_IDLE);
	k_mutex_unlock(&data->i2c_lock);
	return rc;
}

/* ---- API: write_raw (debug aid) -------------------------------------- */

static int at1846s_api_write_raw(const struct device *dev, uint8_t reg, uint8_t hi, uint8_t lo)
{
	return at1846s_write_reg(dev, reg, hi, lo);
}

static int at1846s_api_read_raw(const struct device *dev, uint8_t reg, uint8_t *hi, uint8_t *lo)
{
	return at1846s_read_reg(dev, reg, hi, lo);
}

/* ---- Software squelch thread ----------------------------------------- */

K_THREAD_STACK_DEFINE(at1846s_sq_stack, CONFIG_RADIO_AT1846S_SQUELCH_STACK_SIZE);
static struct k_thread at1846s_sq_thread_obj;

/** Software-squelch worker: polls RSSI noise while RX is active and mutes/unmutes the speaker. */
static void at1846s_squelch_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = p1;
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;

	if (cfg->squelch_hardware) {
		/* Hardware mode: chip manages squelch; this thread is unused. */
		return;
	}

	while (true) {
		k_sem_take(&data->sq_wake, K_FOREVER);

		while (data->rx_active) {
			uint8_t noise;

			if (at1846s_api_get_rssi(dev, NULL, &noise) == 0) {
				bool carrier_open;

				if (data->sq_open) {
					carrier_open = noise < (uint8_t)(data->sq_threshold +
									 AT1846S_SQ_HYSTERESIS);
				} else {
					carrier_open = noise < data->sq_threshold;
				}

				bool new_open;

				if (!carrier_open) {
					new_open = false;
					data->css_miss_count = 0;
				} else if (!data->rx_css_active) {
					new_open = true;
				} else {
					bool css_detected = false;

					(void)at1846s_api_check_css(dev, 0, data->rx_css_type,
								    &css_detected);

					if (css_detected) {
						data->css_miss_count = 0;
						new_open = true;
					} else if (data->sq_open &&
						   data->css_miss_count < AT1846S_CSS_HOLD_POLLS) {
						/* Already open: ride out a brief
						 * tone-detect miss rather than
						 * chopping audio mid-call.
						 */
						data->css_miss_count++;
						new_open = true;
					} else {
						new_open = false;
					}
				}

				if (new_open != data->sq_open) {
					data->sq_open = new_open;
					at1846s_set_speaker(cfg, new_open);
					LOG_DBG("squelch %s noise=%02X th=%02X",
						new_open ? "open" : "close", noise,
						data->sq_threshold);
					if (data->squelch_handler != NULL) {
						data->squelch_handler(dev, new_open);
					}
				}
			}

			k_msleep(CONFIG_RADIO_AT1846S_SQUELCH_POLL_MS);
		}

		/* Exiting RX: ensure speaker is muted. */
		if (data->sq_open) {
			data->sq_open = false;
			at1846s_set_speaker(cfg, false);
			if (data->squelch_handler != NULL) {
				data->squelch_handler(dev, false);
			}
		}
	}
}

/* ---- API table -------------------------------------------------------- */

static const struct radio_trx_api at1846s_api = {
	.set_frequency = at1846s_api_set_frequency,
	.get_frequency = at1846s_api_get_frequency,
	.set_mode = at1846s_api_set_mode,
	.set_bandwidth = at1846s_api_set_bandwidth,
	.set_tx_ctcss = at1846s_api_set_tx_ctcss,
	.set_rx_ctcss = at1846s_api_set_rx_ctcss,
	.set_tx_dcs = at1846s_api_set_tx_dcs,
	.set_rx_dcs = at1846s_api_set_rx_dcs,
	.check_css = at1846s_api_check_css,
	.get_rssi = at1846s_api_get_rssi,
	.set_volume = at1846s_api_set_volume,
	.set_squelch = at1846s_api_set_squelch,
	.set_voice_channel = at1846s_api_set_voice_channel,
	.set_tone1_freq = at1846s_api_set_tone1_freq,
	.register_squelch_cb = at1846s_api_register_squelch_cb,
	.register_css_cb = at1846s_api_register_css_cb,
	.enter_fm_rx = at1846s_api_enter_fm_rx,
	.standby = at1846s_api_standby,
	.write_raw = at1846s_api_write_raw,
	.read_raw = at1846s_api_read_raw,
};

/* ---- Init ------------------------------------------------------------- */

static int at1846s_configure_optional_gpio(const struct gpio_dt_spec *spec, gpio_flags_t flags,
					   const char *name)
{
	if (spec->port == NULL) {
		return 0;
	}
	if (!gpio_is_ready_dt(spec)) {
		LOG_ERR("%s gpio not ready", name);
		return -ENODEV;
	}
	int rc = gpio_pin_configure_dt(spec, flags);
	if (rc < 0) {
		LOG_ERR("%s gpio configure failed (%d)", name, rc);
		return rc;
	}
	return 0;
}

/** Recover the I2C bus, configure optional GPIOs, run the init/postinit tables, and spawn the
 * squelch thread.
 */
static int at1846s_init(const struct device *dev)
{
	const struct at1846s_config *cfg = dev->config;
	struct at1846s_data *data = dev->data;
	int rc;

	k_mutex_init(&data->i2c_lock);
	k_sem_init(&data->sq_wake, 0, 1);
	at1846s_invalidate_cache(data);
	data->current_bank = 0;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("i2c bus not ready");
		return -ENODEV;
	}

	/*
	 * Clears any SDA hold from a prior interrupted transaction. Skip
	 * i2c_configure() after — STM32F4 I2C v1 disables the event IRQ on reconfigure.
	 */
	rc = i2c_recover_bus(cfg->i2c.bus);
	if (rc < 0) {
		LOG_WRN("i2c_recover_bus returned %d (continuing)", rc);
	}

#if AT1846S_HAS_BAND_GPIOS
	for (uint8_t i = 0; i < cfg->band_gpio_count; i++) {
		rc = at1846s_configure_optional_gpio(&cfg->band_gpios[i], GPIO_OUTPUT_INACTIVE,
						     "band-select");
		if (rc < 0) {
			return rc;
		}
	}
#endif

	rc = at1846s_configure_optional_gpio(&cfg->rx_audio_mux_gpio, GPIO_OUTPUT_INACTIVE,
					     "rx-audio-mux");
	if (rc < 0) {
		return rc;
	}
	rc = at1846s_configure_optional_gpio(&cfg->amp_en_gpio, GPIO_OUTPUT_INACTIVE, "amp-en");
	if (rc < 0) {
		return rc;
	}
	rc = at1846s_configure_optional_gpio(&cfg->spk_mute_gpio, GPIO_OUTPUT_ACTIVE, "spk-mute");
	if (rc < 0) {
		return rc;
	}
	rc = at1846s_configure_optional_gpio(&cfg->squelch_gpio, GPIO_INPUT, "squelch");
	if (rc < 0) {
		return rc;
	}
	rc = at1846s_configure_optional_gpio(&cfg->css_gpio, GPIO_INPUT, "css");
	if (rc < 0) {
		return rc;
	}

	rc = at1846s_write_table(dev, at1846s_init_regs, ARRAY_SIZE(at1846s_init_regs));
	if (rc < 0) {
		LOG_ERR("init table write failed (%d)", rc);
		return rc;
	}
	rc = at1846s_write_table(dev, at1846s_postinit_regs, ARRAY_SIZE(at1846s_postinit_regs));
	if (rc < 0) {
		LOG_ERR("postinit table write failed (%d)", rc);
		return rc;
	}

	uint8_t hi = 0, lo = 0;
	rc = at1846s_read_reg(dev, AT1846S_REG_CHIP_ID, &hi, &lo);
	if (rc == 0) {
		LOG_INF("AT1846S init OK; chip id 0x%02X%02X", hi, lo);
	} else {
		LOG_WRN("AT1846S init: chip id read failed (%d)", rc);
	}

	k_thread_create(&at1846s_sq_thread_obj, at1846s_sq_stack,
			K_THREAD_STACK_SIZEOF(at1846s_sq_stack), at1846s_squelch_thread_fn,
			(void *)dev, NULL, NULL, CONFIG_RADIO_AT1846S_SQUELCH_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&at1846s_sq_thread_obj, "at1846s_sq");

	return 0;
}

/* ---- DT instantiation ------------------------------------------------- */

/* Pads band-select-gpios out to AT1846S_BAND_GPIO_MAX; unused slots are
 * {0} sentinels that .port == NULL skips at runtime.
 */
#define AT1846S_BG_OR(inst, i)                                                                     \
	COND_CODE_1(DT_INST_PROP_HAS_IDX(inst, band_select_gpios, i),	\
		    (GPIO_DT_SPEC_INST_GET_BY_IDX(inst, band_select_gpios, i)), \
		    ({0}))

/*
 * Keeps DT_INST_FOREACH_PROP_ELEM_SEP's (,)) separator token intact -- clang-format
 * wants to insert a space that trips checkpatch's SPACING check.
 */
/* clang-format off */
#define AT1846S_INIT(inst)                                                                                  \
	BUILD_ASSERT(DT_INST_PROP_LEN_OR(inst, freq_ranges, 0) % 2 == 0,                                    \
		     "freq-ranges must be (low,high) pairs");                                               \
	static const uint32_t at1846s_freq_ranges_##inst[] = {                                              \
		DT_INST_FOREACH_PROP_ELEM_SEP(inst, freq_ranges, DT_PROP_BY_IDX, (,))};                     \
	static const struct at1846s_config at1846s_cfg_##inst = {                                           \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                          \
		.freq_ranges = at1846s_freq_ranges_##inst,                                                  \
		.freq_range_count = (uint8_t)(DT_INST_PROP_LEN_OR(inst, freq_ranges, 0) / 2),               \
		IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, band_select_gpios), (		\
			.band_gpios = {							\
				AT1846S_BG_OR(inst, 0),					\
				AT1846S_BG_OR(inst, 1),					\
				AT1846S_BG_OR(inst, 2),					\
				AT1846S_BG_OR(inst, 3),					\
			},								\
			.band_gpio_count = (uint8_t)DT_INST_PROP_LEN(inst, band_select_gpios),\
		)) .rx_audio_mux_gpio = \
							  GPIO_DT_SPEC_INST_GET_OR(                         \
								  inst, rx_audio_mux_gpios, {0}),           \
						  .amp_en_gpio = GPIO_DT_SPEC_INST_GET_OR(                  \
							  inst, amp_en_gpios, {0}),                         \
						  .spk_mute_gpio = GPIO_DT_SPEC_INST_GET_OR(                \
							  inst, spk_mute_gpios, {0}),                       \
						  .squelch_gpio = GPIO_DT_SPEC_INST_GET_OR(                 \
							  inst, squelch_gpios, {0}),                        \
						  .css_gpio = GPIO_DT_SPEC_INST_GET_OR(                     \
							  inst, css_detect_gpios, {0}),                     \
						  .baseband_handles_deemph = DT_INST_PROP(                  \
							  inst, baseband_handles_deemphasis),               \
						  .baseband_handles_css = DT_INST_PROP(                     \
							  inst, baseband_handles_ctcss_dcs),                \
						  .squelch_hardware = !strcmp(                              \
							  DT_INST_PROP_OR(inst, squelch_mode,               \
									  "software"),                      \
							  "hardware"),                                      \
						  .tx_freq_offset_hz = DT_INST_PROP_OR(                     \
							  inst, tx_freq_offset_hz, 0),                      \
	};                                                                                                  \
	static struct at1846s_data at1846s_data_##inst;                                                     \
	DEVICE_DT_INST_DEFINE(inst, at1846s_init, NULL, &at1846s_data_##inst, &at1846s_cfg_##inst,          \
			      POST_KERNEL, CONFIG_RADIO_AT1846S_INIT_PRIORITY, &at1846s_api);
/* clang-format on */

DT_INST_FOREACH_STATUS_OKAY(AT1846S_INIT)
