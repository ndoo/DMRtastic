// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/* Decodes the GPIO-driven 8080 bus of the LCD node into controller commands and data. */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <sim/sim_gpio.h>
#include <sim/sim_lcd.h>

#define LCD_NODE DT_NODELABEL(lcd_mipi_dbi)

#define LCD_SPEC(prop) GPIO_DT_SPEC_GET(LCD_NODE, prop)

#define LCD_DATA_BITS 8
#define LCD_MAX_PORTS 4

static const struct gpio_dt_spec s_data[LCD_DATA_BITS] = {
	DT_FOREACH_PROP_ELEM_SEP(LCD_NODE, data_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))};
static const struct gpio_dt_spec s_dc = LCD_SPEC(dc_gpios);
static const struct gpio_dt_spec s_wr = LCD_SPEC(wr_gpios);
static const struct gpio_dt_spec s_rd = LCD_SPEC(rd_gpios);
static const struct gpio_dt_spec s_cs = LCD_SPEC(cs_gpios);
static const struct gpio_dt_spec s_rst = LCD_SPEC(reset_gpios);

struct bus_state {
	bool armed;
	bool wr;
	bool rd;
	bool cs;
	bool dc;
	bool rst;
	uint8_t data;
};

static struct bus_state prev;
static bool prev_valid;

static const struct device *snap_dev[LCD_MAX_PORTS];
static uint32_t snap_wire[LCD_MAX_PORTS];
static uint32_t snap_en[LCD_MAX_PORTS];
static int snap_n;

static int snap_slot(const struct device *port)
{
	for (int i = 0; i < snap_n; i++) {
		if (snap_dev[i] == port) {
			return i;
		}
	}
	snap_dev[snap_n] = port;
	snap_wire[snap_n] = sim_gpio_wire(port);
	snap_en[snap_n] = sim_gpio_out_enable(port);
	return snap_n++;
}

static bool pin_level(const struct gpio_dt_spec *s)
{
	return (snap_wire[snap_slot(s->port)] >> s->pin) & 1U;
}

static bool pin_is_output(const struct gpio_dt_spec *s)
{
	return (snap_en[snap_slot(s->port)] >> s->pin) & 1U;
}

static void snapshot(struct bus_state *b)
{
	snap_n = 0;
	b->armed = pin_is_output(&s_wr) && pin_is_output(&s_cs) && pin_is_output(&s_dc) &&
		   pin_is_output(&s_rd) && pin_is_output(&s_rst);
	b->wr = pin_level(&s_wr);
	b->rd = pin_level(&s_rd);
	b->cs = pin_level(&s_cs);
	b->dc = pin_level(&s_dc);
	b->rst = pin_level(&s_rst);
	b->data = 0;
	for (int i = 0; i < LCD_DATA_BITS; i++) {
		b->data |= pin_level(&s_data[i]) << i;
	}
}

/* Pin polarity: CS and RESET are active low; WR latches on its rising edge. */
static void bus_changed(const struct device *port, void *ctx)
{
	struct bus_state cur;

	ARG_UNUSED(port);
	ARG_UNUSED(ctx);

	snapshot(&cur);
	if (!prev_valid || !cur.armed || !prev.armed) {
		prev = cur;
		prev_valid = true;
		return;
	}

	if (cur.rst != prev.rst) {
		sim_lcd_hw_reset(!cur.rst);
	}
	if (prev.rd && !cur.rd) {
		sim_lcd_violation(SIM_LCD_V_STRAY_RD);
	}

	if (!prev.wr && !cur.wr) {
		if (cur.data != prev.data) {
			sim_lcd_violation(SIM_LCD_V_DATA_WR_LOW);
		}
		if (cur.dc != prev.dc) {
			sim_lcd_violation(SIM_LCD_V_DC_WR_LOW);
		}
		if (cur.cs != prev.cs) {
			sim_lcd_violation(SIM_LCD_V_CS_WR_LOW);
		}
	} else if (!prev.wr && cur.wr) {
		if (cur.data != prev.data || cur.dc != prev.dc || cur.cs != prev.cs) {
			sim_lcd_violation(SIM_LCD_V_WR_RISE_CONCURRENT);
		}
		if (cur.cs) {
			sim_lcd_violation(SIM_LCD_V_WR_NO_CS);
		} else if (!cur.rst) {
			sim_lcd_violation(SIM_LCD_V_WR_IN_RESET);
		} else if (cur.dc) {
			sim_lcd_data(cur.data);
		} else {
			sim_lcd_cmd(cur.data);
		}
	}

	prev = cur;
}

static int sim_lcd_bus_init(void)
{
	sim_gpio_set_listener(s_wr.port, bus_changed, NULL);
	sim_gpio_set_listener(s_cs.port, bus_changed, NULL);
	sim_gpio_set_listener(s_dc.port, bus_changed, NULL);
	sim_gpio_set_listener(s_rd.port, bus_changed, NULL);
	sim_gpio_set_listener(s_rst.port, bus_changed, NULL);
	for (int i = 0; i < LCD_DATA_BITS; i++) {
		sim_gpio_set_listener(s_data[i].port, bus_changed, NULL);
	}
	return 0;
}

SYS_INIT(sim_lcd_bus_init, PRE_KERNEL_2, 0);
