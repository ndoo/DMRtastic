// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#define DT_DRV_COMPAT dmrtastic_sim_hr_c6000

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/radio/radio_baseband.h>

static uint8_t sim_hr_regs[8][256];

static int sim_ok(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int sim_set_fm_tx(const struct device *dev, const struct radio_bb_fm_tx_cfg *cfg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cfg);
	return -ENOTSUP;
}

static int sim_set_dmr(const struct device *dev, const struct radio_bb_dmr_cfg *cfg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cfg);
	return -ENOTSUP;
}

static int sim_set_bool(const struct device *dev, bool v)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(v);
	return 0;
}

static int sim_send_tone(const struct device *dev, uint32_t freq_hz)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(freq_hz);
	return 0;
}

static int sim_set_dtmf(const struct device *dev, uint8_t code)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(code);
	return 0;
}

static int sim_get_rx_ids(const struct device *dev, uint32_t *tg_id, uint32_t *src_id)
{
	ARG_UNUSED(dev);
	*tg_id = 0;
	*src_id = 0;
	return 0;
}

static int sim_get_rx_color_code(const struct device *dev, uint8_t *cc)
{
	ARG_UNUSED(dev);
	*cc = 0;
	return 0;
}

static int sim_get_sync_state(const struct device *dev, bool *has_sync, bool *cc_held,
			      enum radio_bb_waking_mode *waking)
{
	ARG_UNUSED(dev);
	*has_sync = false;
	*cc_held = false;
	*waking = RADIO_BB_WAKING_NONE;
	return 0;
}

static int sim_register_callbacks(const struct device *dev, const struct radio_bb_callbacks *cbs)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cbs);
	return 0;
}

static int sim_write_raw(const struct device *dev, uint8_t page, uint8_t reg, uint8_t val)
{
	ARG_UNUSED(dev);
	if (page >= ARRAY_SIZE(sim_hr_regs)) {
		return -EINVAL;
	}
	sim_hr_regs[page][reg] = val;
	return 0;
}

static int sim_read_raw(const struct device *dev, uint8_t page, uint8_t reg, uint8_t *val)
{
	ARG_UNUSED(dev);
	if (page >= ARRAY_SIZE(sim_hr_regs)) {
		return -EINVAL;
	}
	*val = sim_hr_regs[page][reg];
	return 0;
}

static int sim_set_ids(const struct device *dev, uint32_t tg_id, uint32_t src_id)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(tg_id);
	ARG_UNUSED(src_id);
	return 0;
}

static int sim_set_text(const struct device *dev, const char *text)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(text);
	return 0;
}

static int sim_set_location(const struct device *dev, int32_t lat, int32_t lon)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(lat);
	ARG_UNUSED(lon);
	return 0;
}

static int sim_set_gain(const struct device *dev, int8_t gain)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(gain);
	return 0;
}

static int sim_inject_ambe(const struct device *dev, const uint8_t *frame)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(frame);
	return 0;
}

static const struct radio_bb_api sim_hr_c6000_api = {
	.set_fm_rx = sim_ok,
	.set_fm_tx = sim_set_fm_tx,
	.set_dmr = sim_set_dmr,
	.init_digital = sim_ok,
	.terminate_digital = sim_ok,
	.set_audio_path = sim_set_bool,
	.set_mic = sim_set_bool,
	.mute_audio = sim_set_bool,
	.send_tone = sim_send_tone,
	.set_dtmf = sim_set_dtmf,
	.dtmf_off = sim_set_bool,
	.get_rx_ids = sim_get_rx_ids,
	.get_rx_color_code = sim_get_rx_color_code,
	.clear_rx_ids = sim_ok,
	.reset_sync = sim_ok,
	.set_dmr_ids = sim_set_ids,
	.set_talker_alias = sim_set_text,
	.set_talker_alias_location = sim_set_location,
	.set_rx_gain = sim_set_gain,
	.set_agc_gain = sim_set_gain,
	.inject_ambe_frame = sim_inject_ambe,
	.flush_audio = sim_ok,
	.get_sync_state = sim_get_sync_state,
	.register_callbacks = sim_register_callbacks,
	.power_down = sim_set_bool,
	.write_raw = sim_write_raw,
	.read_raw = sim_read_raw,
};

#define SIM_HR_C6000_INIT(n)                                                                       \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &sim_hr_c6000_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_HR_C6000_INIT)
