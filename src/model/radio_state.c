// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include "radio_state.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <drivers/radio/radio_transceiver.h>

static const struct device *trx_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(at1846s));
}

static const struct radio_trx_api *trx_api(void)
{
	return (const struct radio_trx_api *)trx_dev()->api;
}

int radio_state_set_frequency(uint32_t freq_hz)
{
	return trx_api()->set_frequency(trx_dev(), freq_hz, false);
}

int radio_state_get_frequency(uint32_t *freq_hz)
{
	const struct radio_trx_api *api = trx_api();

	if (!api->get_frequency) {
		return -ENOSYS;
	}
	return api->get_frequency(trx_dev(), freq_hz);
}

void radio_state_get_rssi(uint8_t *signal, uint8_t *noise)
{
	trx_api()->get_rssi(trx_dev(), signal, noise);
}

/*
 * Map signal byte (0-255, rises with carrier) to 0-4 bars.
 * Thresholds are approximate; tune after over-air testing.
 */
static uint8_t rssi_to_bars(uint8_t signal)
{
	if (signal >= 100) return 4;
	if (signal >= 70)  return 3;
	if (signal >= 45)  return 2;
	if (signal >= 20)  return 1;
	return 0;
}

uint8_t radio_state_get_rssi_bars(void)
{
	uint8_t signal = 0, noise = 0;

	trx_api()->get_rssi(trx_dev(), &signal, &noise);
	return rssi_to_bars(signal);
}

int radio_state_set_squelch(uint8_t level)
{
	return trx_api()->set_squelch(trx_dev(), level);
}

int radio_state_set_bandwidth(bool is_25k)
{
	return trx_api()->set_bandwidth(trx_dev(), is_25k ? RADIO_BW_25K : RADIO_BW_12K5);
}

int radio_state_set_volume(uint8_t pct)
{
	return trx_api()->set_volume(trx_dev(), pct);
}

int radio_state_set_rx_css(const struct cp_css *css)
{
	const struct radio_trx_api *api = trx_api();

	switch (css->type) {
	case CP_CSS_DCS:
		return api->set_rx_dcs(trx_dev(), css->value, css->inverted);
	case CP_CSS_CTCSS:
		return api->set_rx_ctcss(trx_dev(), css->value);
	case CP_CSS_NONE:
	default:
		return api->set_rx_ctcss(trx_dev(), 0);
	}
}

int radio_state_set_tx_css(const struct cp_css *css)
{
	const struct radio_trx_api *api = trx_api();

	switch (css->type) {
	case CP_CSS_DCS:
		return api->set_tx_dcs(trx_dev(), css->value, css->inverted);
	case CP_CSS_CTCSS:
		return api->set_tx_ctcss(trx_dev(), css->value);
	case CP_CSS_NONE:
	default:
		return api->set_tx_ctcss(trx_dev(), 0);
	}
}
