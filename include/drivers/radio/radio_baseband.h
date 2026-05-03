/*
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 * SPDX-License-Identifier: MIT
 *
 * Generic DMR/FM baseband modem driver API. The driver-specific
 * implementation (e.g. HR-C6000) populates a const struct radio_bb_api
 * and publishes it as the device's api pointer.
 *
 * All callbacks fire from interrupt context; receivers must keep work
 * short and non-blocking. The driver itself owns all DMR timing and
 * runs the slot state machine internally.
 */

#ifndef DMRTASTIC_DRIVERS_RADIO_RADIO_BASEBAND_H_
#define DMRTASTIC_DRIVERS_RADIO_RADIO_BASEBAND_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

enum radio_bb_waking_mode {
	RADIO_BB_WAKING_NONE    = 0,
	RADIO_BB_WAKING_WAITING = 1,
	RADIO_BB_WAKING_AWAKEN  = 2,
	RADIO_BB_WAKING_FAILED  = 3,
};

enum radio_bb_embedded_type {
	RADIO_BB_EMBEDDED_GROUP             = 0,
	RADIO_BB_EMBEDDED_USER_USER         = 1,
	RADIO_BB_EMBEDDED_TALKER_ALIAS_HDR  = 2,
	RADIO_BB_EMBEDDED_TALKER_ALIAS_BLK1 = 3,
	RADIO_BB_EMBEDDED_TALKER_ALIAS_BLK2 = 4,
	RADIO_BB_EMBEDDED_TALKER_ALIAS_BLK3 = 5,
	RADIO_BB_EMBEDDED_GPS_INFO          = 6,
};

struct radio_bb_fm_tx_cfg {
	uint8_t  i_gain;
	uint8_t  q_gain;
	uint8_t  i_offset;
	uint8_t  q_offset;
	int8_t   mod2_ref_offset;
	uint8_t  deviation;
	uint8_t  mic_gain;
	uint32_t ctcss_tone_dHz;
	uint16_t dcs_code;
	bool     dcs_inverted;
};

struct radio_bb_dmr_cfg {
	uint8_t i_gain;
	uint8_t q_gain;
	uint8_t i_offset;
	uint8_t q_offset;
	int8_t  mod2_ref_offset;
	uint8_t color_code;
	uint8_t timeslot;
};

struct radio_bb_rx_frame {
	uint8_t lc[12];
	uint8_t ambe[27];
	uint8_t embedded_type;
	bool    lc_valid;
	bool    has_audio;
};

typedef void (*radio_bb_rx_frame_fn_t)(const struct radio_bb_rx_frame *frame);
typedef void (*radio_bb_tx_done_fn_t)(bool failed);

struct radio_bb_callbacks {
	radio_bb_rx_frame_fn_t on_rx_frame;
	radio_bb_tx_done_fn_t  on_tx_done;
};

struct radio_bb_api {
	int (*set_fm_rx)(const struct device *dev);
	int (*set_fm_tx)(const struct device *dev,
			 const struct radio_bb_fm_tx_cfg *cfg);
	int (*set_dmr)(const struct device *dev,
		       const struct radio_bb_dmr_cfg *cfg);

	int (*init_digital)(const struct device *dev);
	int (*terminate_digital)(const struct device *dev);

	int (*set_audio_path)(const struct device *dev, bool fm_feedthrough);
	int (*set_mic)(const struct device *dev, bool enable);
	int (*mute_audio)(const struct device *dev, bool mute);

	int (*send_tone)(const struct device *dev, uint32_t freq_hz);
	int (*set_dtmf)(const struct device *dev, uint8_t code);
	int (*dtmf_off)(const struct device *dev, bool enable_mic);

	int (*get_rx_ids)(const struct device *dev,
			  uint32_t *tg_id, uint32_t *src_id);
	int (*get_rx_color_code)(const struct device *dev, uint8_t *cc);
	int (*clear_rx_ids)(const struct device *dev);
	int (*reset_sync)(const struct device *dev);

	int (*set_dmr_ids)(const struct device *dev,
			   uint32_t tg_id, uint32_t src_id);
	int (*set_talker_alias)(const struct device *dev, const char *text);
	int (*set_talker_alias_location)(const struct device *dev,
					 int32_t lat_1e7, int32_t lon_1e7);

	int (*set_rx_gain)(const struct device *dev, int8_t gain);
	int (*set_agc_gain)(const struct device *dev, int8_t gain);

	int (*inject_ambe_frame)(const struct device *dev, const uint8_t *frame);
	int (*flush_audio)(const struct device *dev);

	int (*get_sync_state)(const struct device *dev,
			      bool *has_sync, bool *cc_held,
			      enum radio_bb_waking_mode *waking);

	int (*register_callbacks)(const struct device *dev,
				  const struct radio_bb_callbacks *cbs);

	int (*power_down)(const struct device *dev, bool down);
};

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_DRIVERS_RADIO_RADIO_BASEBAND_H_ */
