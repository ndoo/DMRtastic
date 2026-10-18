// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/* Keypad matrix, PTT, encoder, volume pot and battery models behind the simulated GPIO and ADC. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <sim/sim_ctl.h>
#include <sim/sim_gpio.h>

#define KEYPAD_NODE DT_NODELABEL(keypad)
#define ROWS        DT_PROP_LEN(KEYPAD_NODE, row_gpios)
#define COLS        DT_PROP_LEN(KEYPAD_NODE, col_gpios)

static const struct gpio_dt_spec s_rows[ROWS] = {
	DT_FOREACH_PROP_ELEM_SEP(KEYPAD_NODE, row_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))};
static const struct gpio_dt_spec s_cols[COLS] = {
	DT_FOREACH_PROP_ELEM_SEP(KEYPAD_NODE, col_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))};

static const struct gpio_dt_spec s_ptt =
	GPIO_DT_SPEC_GET(DT_NODELABEL(ptt), gpios);

#define ENC_NODE DT_NODELABEL(rotary_encoder)
static const struct gpio_dt_spec s_enc[2] = {GPIO_DT_SPEC_GET_BY_IDX(ENC_NODE, gpios, 0),
					     GPIO_DT_SPEC_GET_BY_IDX(ENC_NODE, gpios, 1)};

#define VOL_NODE  DT_NODELABEL(vol_axis_ch)
#define BATT_NODE DT_NODELABEL(battery)

static const struct adc_dt_spec s_vol_adc = ADC_DT_SPEC_GET_BY_IDX(VOL_NODE, 0);
static const struct adc_dt_spec s_batt_adc = ADC_DT_SPEC_GET(BATT_NODE);

/* Keymap positions: row = sensed pin index, col = driven line index. */
static const struct {
	const char *name;
	uint8_t row;
	uint8_t col;
} keys[] = {
	{"1", 0, 0},     {"2", 1, 0},    {"3", 2, 0},  {"4", 3, 0},    {"5", 4, 0},
	{"6", 5, 0},     {"0", 6, 0},    {"star", 7, 0}, {"enter", 0, 1}, {"up", 1, 1},
	{"down", 2, 1},  {"7", 3, 1},    {"8", 4, 1},  {"9", 5, 1},    {"pound", 6, 1},
	{"back", 7, 1},  {"sk2", 6, 2},  {"sk1", 7, 2},
};

static atomic_t key_down[ARRAY_SIZE(keys)];

#define KEY_TAP_MS 120

static bool col_driven_high(int col)
{
	const struct gpio_dt_spec *c = &s_cols[col];

	return (sim_gpio_out_enable(c->port) & sim_gpio_out_latch(c->port) & BIT(c->pin)) != 0;
}

static uint32_t matrix_peer(void *ctx, uint32_t *mask)
{
	const struct device *port = ctx;
	uint32_t level = 0;

	for (int r = 0; r < ROWS; r++) {
		if (s_rows[r].port != port) {
			continue;
		}
		*mask |= BIT(s_rows[r].pin);
		for (size_t k = 0; k < ARRAY_SIZE(keys); k++) {
			if (keys[k].row == r && atomic_get(&key_down[k]) &&
			    col_driven_high(keys[k].col)) {
				level |= BIT(s_rows[r].pin);
			}
		}
	}
	return level;
}

static int find_key(const char *name)
{
	for (size_t k = 0; k < ARRAY_SIZE(keys); k++) {
		if (strcmp(keys[k].name, name) == 0) {
			return k;
		}
	}
	return -1;
}

/* "key <name> [down|up|tap]" */
static int cmd_key(int argc, char **argv)
{
	int k;

	if (argc < 2) {
		return -EINVAL;
	}
	k = find_key(argv[1]);
	if (k < 0) {
		return -ENOENT;
	}
	if (argc >= 3 && strcmp(argv[2], "down") == 0) {
		atomic_set(&key_down[k], 1);
	} else if (argc >= 3 && strcmp(argv[2], "up") == 0) {
		atomic_set(&key_down[k], 0);
	} else {
		atomic_set(&key_down[k], 1);
		k_msleep(KEY_TAP_MS);
		atomic_set(&key_down[k], 0);
		k_msleep(KEY_TAP_MS);
	}
	return 0;
}

static void press(const struct gpio_dt_spec *s, bool down)
{
	if (down) {
		sim_gpio_force(s->port, BIT(s->pin), 0);
	} else {
		sim_gpio_unforce(s->port, BIT(s->pin));
	}
}

/* "ptt down|up" */
static int cmd_ptt(int argc, char **argv)
{
	if (argc < 2) {
		return -EINVAL;
	}
	press(&s_ptt, strcmp(argv[1], "down") == 0);
	return 0;
}

/* Gray-code phases of the two active-low lines, as physical levels {A, B}. */
static const uint8_t enc_phase[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
static int enc_pos;

static void enc_step(int dir)
{
	enc_pos = (enc_pos + dir + 4) % 4;
	for (int i = 0; i < 2; i++) {
		sim_gpio_force(s_enc[i].port, BIT(s_enc[i].pin), enc_phase[enc_pos][i] ? BIT(s_enc[i].pin) : 0);
	}
	k_msleep(2);
}

/* "enc <n>": n clockwise detents, negative for counter-clockwise; two edges per detent. */
static int cmd_enc(int argc, char **argv)
{
	long n;

	if (argc < 2) {
		return -EINVAL;
	}
	n = strtol(argv[1], NULL, 0);
	for (long i = 0; i < labs(n) * 2; i++) {
		enc_step(n > 0 ? -1 : 1);
	}
	return 0;
}

#define ADC_FULL_SCALE_MV 3300
#define ADC_COUNTS        4096

/* "vol <raw>" sets the volume pot's ADC reading. */
static int cmd_vol(int argc, char **argv)
{
	if (argc < 2) {
		return -EINVAL;
	}
	return adc_emul_const_raw_value_set(s_vol_adc.dev, s_vol_adc.channel_id,
					    strtoul(argv[1], NULL, 0));
}

static int set_battery_mv(uint32_t pack_mv)
{
	uint32_t pin_mv = pack_mv * DT_PROP(BATT_NODE, output_ohms) / DT_PROP(BATT_NODE, full_ohms);

	return adc_emul_const_raw_value_set(s_batt_adc.dev, s_batt_adc.channel_id,
					    pin_mv * ADC_COUNTS / ADC_FULL_SCALE_MV);
}

/* "batt <pack mV>" */
static int cmd_batt(int argc, char **argv)
{
	if (argc < 2) {
		return -EINVAL;
	}
	return set_battery_mv(strtoul(argv[1], NULL, 0));
}

#define DEFAULT_VOL_RAW 900
#define DEFAULT_BATT_MV 7800

static int sim_input_init(void)
{
	const struct device *ports[ROWS];
	int n = 0;

	for (int r = 0; r < ROWS; r++) {
		bool seen = false;

		for (int i = 0; i < n; i++) {
			seen |= ports[i] == s_rows[r].port;
		}
		if (!seen) {
			ports[n++] = s_rows[r].port;
			sim_gpio_set_peer(s_rows[r].port, matrix_peer, (void *)s_rows[r].port);
		}
	}

	adc_emul_const_raw_value_set(s_vol_adc.dev, s_vol_adc.channel_id, DEFAULT_VOL_RAW);
	set_battery_mv(DEFAULT_BATT_MV);

	sim_ctl_register("key", cmd_key);
	sim_ctl_register("ptt", cmd_ptt);
	sim_ctl_register("enc", cmd_enc);
	sim_ctl_register("vol", cmd_vol);
	sim_ctl_register("batt", cmd_batt);
	return 0;
}

SYS_INIT(sim_input_init, APPLICATION, 50);
