// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#define DT_DRV_COMPAT dmrtastic_sim_at1846s

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <stdlib.h>

#include <zephyr/init.h>

#include <drivers/radio/radio_transceiver.h>

#include <sim/sim_ctl.h>
#include <sim/sim_radio.h>

#define SIM_NOISE_IDLE     120
#define SIM_NOISE_FLOOR    8
#define SIM_SQUELCH_HYST   3
#define SIM_MAX_SIGNALS    8
#define SIM_CHANNEL_HALF_HZ 6250U

struct sim_signal {
	uint32_t freq_hz;
	uint8_t level;
};

struct sim_at1846s_data {
	uint16_t regs[128];
	uint32_t rx_hz;
	uint32_t tx_hz;
	uint8_t volume;
	uint8_t squelch;
	bool sq_open;
	radio_trx_squelch_fn_t squelch_cb;
	radio_trx_css_fn_t css_cb;
};

static struct sim_at1846s_data sim_at1846s_data;
static struct sim_signal signals[SIM_MAX_SIGNALS];
static int nsignals;

static uint8_t sim_signal_level(uint32_t rx_hz)
{
	uint8_t best = 0;

	for (int i = 0; i < nsignals; i++) {
		uint32_t diff = rx_hz > signals[i].freq_hz ? rx_hz - signals[i].freq_hz
							   : signals[i].freq_hz - rx_hz;

		if (diff <= SIM_CHANNEL_HALF_HZ && signals[i].level > best) {
			best = signals[i].level;
		}
	}
	return best;
}

static void sim_noise_signal(const struct sim_at1846s_data *d, uint8_t *signal, uint8_t *noise)
{
	uint8_t level = sim_signal_level(d->rx_hz);

	*signal = level;
	*noise = level == 0 ? SIM_NOISE_IDLE : MAX(SIM_NOISE_FLOOR, SIM_NOISE_IDLE - level);
}

static void sim_squelch_update(const struct device *dev)
{
	struct sim_at1846s_data *d = dev->data;
	uint8_t signal;
	uint8_t noise;
	bool open = d->sq_open;

	sim_noise_signal(d, &signal, &noise);
	if (!open && noise < d->squelch) {
		open = true;
	} else if (open && noise > d->squelch + SIM_SQUELCH_HYST) {
		open = false;
	}
	if (open != d->sq_open) {
		d->sq_open = open;
		if (d->squelch_cb != NULL) {
			d->squelch_cb(dev, open);
		}
	}
}

static int sim_set_frequency(const struct device *dev, uint32_t freq_hz, bool tx)
{
	struct sim_at1846s_data *d = dev->data;

	uint32_t reg = (uint32_t)((uint64_t)freq_hz * 2 / 125);

	if (tx) {
		d->tx_hz = freq_hz;
		return 0;
	}
	d->rx_hz = freq_hz;
	d->regs[0x29] = reg >> 16;
	d->regs[0x2A] = reg & 0xFFFF;
	sim_squelch_update(dev);
	return 0;
}

static int sim_get_frequency(const struct device *dev, uint32_t *freq_hz)
{
	struct sim_at1846s_data *d = dev->data;

	*freq_hz = d->rx_hz;
	return 0;
}

static int sim_ok_mode(const struct device *dev, enum radio_trx_mode mode)
{
	ARG_UNUSED(dev);
	return mode == RADIO_TRX_MODE_FM ? 0 : -ENOTSUP;
}

static int sim_ok_bw(const struct device *dev, enum radio_bw bw)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(bw);
	return 0;
}

static int sim_ok_tone(const struct device *dev, uint16_t tone)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(tone);
	return 0;
}

static int sim_ok_dcs(const struct device *dev, uint16_t code, bool inv)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(code);
	ARG_UNUSED(inv);
	return 0;
}

static int sim_check_css(const struct device *dev, uint16_t tone, enum radio_css_type type,
			 bool *detected)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(tone);
	ARG_UNUSED(type);
	*detected = false;
	return 0;
}

static int sim_get_rssi(const struct device *dev, uint8_t *signal, uint8_t *noise)
{
	sim_noise_signal(dev->data, signal, noise);
	return 0;
}

static int sim_set_volume(const struct device *dev, uint8_t pct)
{
	struct sim_at1846s_data *d = dev->data;

	d->volume = MIN(pct, 100);
	return 0;
}

static int sim_set_squelch(const struct device *dev, uint8_t level)
{
	struct sim_at1846s_data *d = dev->data;

	d->squelch = level;
	sim_squelch_update(dev);
	return 0;
}

static int sim_set_voice_channel(const struct device *dev, enum radio_voice_ch ch, uint8_t gain,
				 uint16_t deviation)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ch);
	ARG_UNUSED(gain);
	ARG_UNUSED(deviation);
	return 0;
}

static int sim_register_squelch_cb(const struct device *dev, radio_trx_squelch_fn_t fn)
{
	struct sim_at1846s_data *d = dev->data;

	d->squelch_cb = fn;
	return 0;
}

static int sim_register_css_cb(const struct device *dev, radio_trx_css_fn_t fn)
{
	struct sim_at1846s_data *d = dev->data;

	d->css_cb = fn;
	return 0;
}

static int sim_noop(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int sim_write_raw(const struct device *dev, uint8_t reg, uint8_t hi, uint8_t lo)
{
	struct sim_at1846s_data *d = dev->data;

	if (reg >= ARRAY_SIZE(d->regs)) {
		return -EINVAL;
	}
	d->regs[reg] = ((uint16_t)hi << 8) | lo;
	return 0;
}

static int sim_read_raw(const struct device *dev, uint8_t reg, uint8_t *hi, uint8_t *lo)
{
	struct sim_at1846s_data *d = dev->data;
	uint8_t signal;
	uint8_t noise;

	if (reg >= ARRAY_SIZE(d->regs)) {
		return -EINVAL;
	}
	if (reg == 0x1B) {
		sim_noise_signal(d, &signal, &noise);
		*hi = signal;
		*lo = noise;
		return 0;
	}
	*hi = d->regs[reg] >> 8;
	*lo = d->regs[reg] & 0xFF;
	return 0;
}

static const struct radio_trx_api sim_at1846s_api = {
	.set_frequency = sim_set_frequency,
	.get_frequency = sim_get_frequency,
	.set_mode = sim_ok_mode,
	.set_bandwidth = sim_ok_bw,
	.set_tx_ctcss = sim_ok_tone,
	.set_rx_ctcss = sim_ok_tone,
	.set_tx_dcs = sim_ok_dcs,
	.set_rx_dcs = sim_ok_dcs,
	.check_css = sim_check_css,
	.get_rssi = sim_get_rssi,
	.set_volume = sim_set_volume,
	.set_squelch = sim_set_squelch,
	.set_voice_channel = sim_set_voice_channel,
	.set_tone1_freq = sim_ok_tone,
	.register_squelch_cb = sim_register_squelch_cb,
	.register_css_cb = sim_register_css_cb,
	.enter_fm_rx = sim_noop,
	.standby = sim_noop,
	.write_raw = sim_write_raw,
	.read_raw = sim_read_raw,
};

int sim_radio_set_signal(uint32_t freq_hz, uint8_t level)
{
	for (int i = 0; i < nsignals; i++) {
		if (signals[i].freq_hz == freq_hz) {
			signals[i].level = level;
			goto done;
		}
	}
	if (nsignals >= SIM_MAX_SIGNALS) {
		return -ENOMEM;
	}
	signals[nsignals].freq_hz = freq_hz;
	signals[nsignals].level = level;
	nsignals++;
done:
	sim_squelch_update(DEVICE_DT_GET(DT_NODELABEL(at1846s)));
	return 0;
}

void sim_radio_clear_signals(void)
{
	nsignals = 0;
	sim_squelch_update(DEVICE_DT_GET(DT_NODELABEL(at1846s)));
}

/* "signal <MHz|Hz> <level>" adds a signal; "signal clear" removes all. */
static int cmd_signal(int argc, char **argv)
{
	char *end;
	double v;

	if (argc == 2 && strcmp(argv[1], "clear") == 0) {
		sim_radio_clear_signals();
		return 0;
	}
	if (argc != 3) {
		return -EINVAL;
	}
	v = strtod(argv[1], &end);
	if (v < 1000000.0) {
		v *= 1000000.0;
	}
	return sim_radio_set_signal((uint32_t)(v + 0.5), strtoul(argv[2], NULL, 0));
}

static int sim_at1846s_ctl_init(void)
{
	return sim_ctl_register("signal", cmd_signal);
}

SYS_INIT(sim_at1846s_ctl_init, POST_KERNEL, 2);

#define SIM_AT1846S_INIT(n)                                                                        \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &sim_at1846s_data, NULL, POST_KERNEL,                 \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &sim_at1846s_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_AT1846S_INIT)
