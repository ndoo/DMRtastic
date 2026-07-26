/*
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 * SPDX-License-Identifier: MIT
 *
 * Generic RF-transceiver driver API; a driver (e.g. AT1846S) populates a
 * const struct radio_trx_api as the device's api pointer. Synchronous,
 * thread-context only — never call from ISR context.
 */

#ifndef DMRTASTIC_DRIVERS_RADIO_RADIO_TRANSCEIVER_H_
#define DMRTASTIC_DRIVERS_RADIO_RADIO_TRANSCEIVER_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

enum radio_trx_mode {
	RADIO_TRX_MODE_FM  = 0,
	RADIO_TRX_MODE_DMR = 1,
};

enum radio_bw {
	RADIO_BW_12K5 = 0,
	RADIO_BW_25K  = 1,
};

/* Voice-channel mux selection, encoded as the chip's voice-select register
 * high-byte value so drivers can write it directly under a preserve mask. */
enum radio_voice_ch {
	RADIO_VOICE_CH_NONE  = 0x00,
	RADIO_VOICE_CH_TONE1 = 0x10,
	RADIO_VOICE_CH_TONE2 = 0x20,
	RADIO_VOICE_CH_DTMF  = 0x30,
	RADIO_VOICE_CH_MIC   = 0x40,
};

enum radio_css_type {
	RADIO_CSS_CTCSS    = 0,
	RADIO_CSS_DCS_NORM = 1,
	RADIO_CSS_DCS_INV  = 2,
};

typedef void (*radio_trx_squelch_fn_t)(const struct device *dev, bool open);
typedef void (*radio_trx_css_fn_t)(const struct device *dev, bool detected);

struct radio_trx_api {
	/** Program the RX or TX frequency; tx selects which and applies any TX offset. */
	int (*set_frequency)(const struct device *dev, uint32_t freq_hz, bool tx);

	/** Last RX frequency set via set_frequency(dev, hz, false); *freq_hz is 0 if never RX-tuned. */
	int (*get_frequency)(const struct device *dev, uint32_t *freq_hz);

	int (*set_mode)(const struct device *dev, enum radio_trx_mode mode);
	int (*set_bandwidth)(const struct device *dev, enum radio_bw bw);

	int (*set_tx_ctcss)(const struct device *dev, uint16_t tone_dHz);
	int (*set_rx_ctcss)(const struct device *dev, uint16_t tone_dHz);
	int (*set_tx_dcs)(const struct device *dev, uint16_t code, bool inv);
	int (*set_rx_dcs)(const struct device *dev, uint16_t code, bool inv);

	int (*check_css)(const struct device *dev, uint16_t tone,
			 enum radio_css_type type, bool *detected);

	int (*get_rssi)(const struct device *dev,
			uint8_t *signal, uint8_t *noise);

	/** Set RX volume as a percentage (0-100, clamped); drivers map it to native range. */
	int (*set_volume)(const struct device *dev, uint8_t pct);

	/** Set squelch threshold; scale is driver-native (e.g. AT1846S noise byte). */
	int (*set_squelch)(const struct device *dev, uint8_t level);

	/** Route the voice mux to ch; gain/deviation are driver-specific and may be ignored. */
	int (*set_voice_channel)(const struct device *dev,
				 enum radio_voice_ch ch,
				 uint8_t gain, uint16_t deviation);

	int (*set_tone1_freq)(const struct device *dev, uint16_t tone_freq_val);

	int (*register_squelch_cb)(const struct device *dev,
				   radio_trx_squelch_fn_t fn);
	int (*register_css_cb)(const struct device *dev,
			       radio_trx_css_fn_t fn);

	/** Enter FM RX, driving any board-side audio mux/amp/unmute lines wired to this device. */
	int (*enter_fm_rx)(const struct device *dev);
	/** Return the chip to low-power idle. */
	int (*standby)(const struct device *dev);

	int (*write_raw)(const struct device *dev,
			 uint8_t reg, uint8_t hi, uint8_t lo);
	int (*read_raw)(const struct device *dev,
			uint8_t reg, uint8_t *hi, uint8_t *lo);
};

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_DRIVERS_RADIO_RADIO_TRANSCEIVER_H_ */
