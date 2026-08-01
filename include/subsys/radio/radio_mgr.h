/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Radio manager: orchestrates the RF transceiver and DMR baseband, owns
 * the board-level PA/LNA/amp/mic GPIOs and TX power DAC. Application code
 * should use only this API, never the underlying chip drivers directly.
 */

#ifndef DMRTASTIC_SUBSYS_RADIO_RADIO_MGR_H_
#define DMRTASTIC_SUBSYS_RADIO_RADIO_MGR_H_

#include <stdbool.h>
#include <stdint.h>
#include <drivers/radio/radio_baseband.h>
#include <drivers/radio/radio_transceiver.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up the transceiver and baseband devices and their board-level GPIOs. */
int radio_mgr_init(void);

/** Tune RX/TX frequencies (TX offset compensation applied internally) and set mode/bandwidth. */
int radio_mgr_set_channel(uint32_t rx_hz, uint32_t tx_hz, enum radio_trx_mode mode,
			  enum radio_bw bw);

int radio_mgr_set_dmr_cfg(uint8_t color_code, uint8_t timeslot);
/** Set baseband I/Q modulator gains (driver-native scale). */
int radio_mgr_set_tx_gains(uint8_t i_gain, uint8_t q_gain);

/** Key up: enable PA/mic and switch the baseband/transceiver into TX. */
int radio_mgr_ptt_push(void);
/** Un-key: return to RX and disable PA/mic. */
int radio_mgr_ptt_release(void);

/** Set TX power via the PA DAC channel (driver-native level, not dBm/watts). */
int radio_mgr_set_tx_power(uint8_t level);
int radio_mgr_set_volume(uint8_t level);

int radio_mgr_set_tx_ctcss(uint16_t tone_dHz);
int radio_mgr_set_rx_ctcss(uint16_t tone_dHz);
int radio_mgr_set_tx_dcs(uint16_t code, bool inv);
int radio_mgr_set_rx_dcs(uint16_t code, bool inv);

int radio_mgr_get_rssi(uint8_t *signal, uint8_t *noise);

int radio_mgr_inject_tone(uint32_t freq_hz);
int radio_mgr_inject_dtmf(uint8_t code);

void radio_mgr_register_dmr_callbacks(const struct radio_bb_callbacks *cbs);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_SUBSYS_RADIO_RADIO_MGR_H_ */
