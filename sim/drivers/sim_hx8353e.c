// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cmdline.h>
#include <soc.h>

#include <sim/sim_ctl.h>
#include <sim/sim_host.h>
#include <sim/sim_lcd.h>
#include <sim/sim_png.h>

LOG_MODULE_REGISTER(sim_lcd, LOG_LEVEL_INF);

#define GRAM_COLS 132
#define GRAM_ROWS 162

#define CMD_NOP    0x00
#define CMD_SWRST  0x01
#define CMD_SLPIN  0x10
#define CMD_SLPOUT 0x11
#define CMD_NORON  0x13
#define CMD_INVOFF 0x20
#define CMD_INVON  0x21
#define CMD_DISPOFF 0x28
#define CMD_DISPON 0x29
#define CMD_CASET  0x2A
#define CMD_PASET  0x2B
#define CMD_RAMWR  0x2C
#define CMD_MADCTL 0x36
#define CMD_COLMOD 0x3A

#define MADCTL_MY BIT(7)
#define MADCTL_MX BIT(6)
#define MADCTL_MV BIT(5)

#define SLPOUT_SETTLE_MS 120
#define GRAM_FILL        0xF81F

static const char *const violation_text[] = {
#define X(id, text) [SIM_LCD_V_##id] = text,
	SIM_LCD_VIOLATIONS(X)
#undef X
};

static uint32_t violation_counts[SIM_LCD_V_COUNT];

static uint16_t gram[GRAM_ROWS][GRAM_COLS];

static struct {
	bool in_reset;
	bool sleeping;
	bool display_on;
	bool inverted;
	uint8_t madctl;
	uint8_t colmod;
	uint16_t xs, xe, ys, ye;
	uint16_t cx, cy;
	uint8_t cmd;
	uint8_t nparam;
	uint8_t pbuf[4];
	uint8_t pix[3];
	uint8_t npix;
	int64_t slpout_ms;
	uint32_t cmds;
	uint32_t data_bytes;
	uint32_t pixels;
} lcd;

static char *png_path;

void sim_lcd_violation(enum sim_lcd_violation v)
{
	if (violation_counts[v]++ == 0) {
		LOG_WRN("protocol violation: %s", violation_text[v]);
	}
}

uint32_t sim_lcd_violation_count(enum sim_lcd_violation v)
{
	return violation_counts[v];
}

uint32_t sim_lcd_violation_total(void)
{
	uint32_t n = 0;

	for (int i = 0; i < SIM_LCD_V_COUNT; i++) {
		n += violation_counts[i];
	}
	return n;
}

static void lcd_state_reset(void)
{
	lcd.sleeping = true;
	lcd.display_on = false;
	lcd.inverted = false;
	lcd.madctl = 0;
	lcd.colmod = 0x06;
	lcd.xs = 0;
	lcd.xe = GRAM_COLS - 1;
	lcd.ys = 0;
	lcd.ye = GRAM_ROWS - 1;
	lcd.cmd = CMD_NOP;
	lcd.nparam = 0;
	lcd.npix = 0;
	lcd.slpout_ms = INT64_MIN / 2;
}

void sim_lcd_hw_reset(bool asserted)
{
	lcd.in_reset = asserted;
	if (asserted) {
		lcd_state_reset();
	}
}

static uint16_t logical_w(void)
{
	return (lcd.madctl & MADCTL_MV) ? GRAM_ROWS : GRAM_COLS;
}

static uint16_t logical_h(void)
{
	return (lcd.madctl & MADCTL_MV) ? GRAM_COLS : GRAM_ROWS;
}

static void put_pixel(uint16_t v)
{
	uint16_t col;
	uint16_t row;

	lcd.pixels++;
	if (lcd.cx >= logical_w() || lcd.cy >= logical_h()) {
		sim_lcd_violation(SIM_LCD_V_WINDOW_OOB);
	} else {
		if (lcd.madctl & MADCTL_MV) {
			col = lcd.cy;
			row = lcd.cx;
		} else {
			col = lcd.cx;
			row = lcd.cy;
		}
		if (lcd.madctl & MADCTL_MX) {
			col = GRAM_COLS - 1 - col;
		}
		if (lcd.madctl & MADCTL_MY) {
			row = GRAM_ROWS - 1 - row;
		}
		gram[row][col] = v;
	}

	if (++lcd.cx > lcd.xe) {
		lcd.cx = lcd.xs;
		if (++lcd.cy > lcd.ye) {
			lcd.cy = lcd.ys;
		}
	}
}

void sim_lcd_cmd(uint8_t cmd)
{
	int64_t now = k_uptime_get();

	lcd.cmds++;
	lcd.nparam = 0;
	lcd.npix = 0;
	lcd.cmd = cmd;

	if (lcd.in_reset) {
		sim_lcd_violation(SIM_LCD_V_CMD_IN_RESET);
	}
	if (now - lcd.slpout_ms < SLPOUT_SETTLE_MS && cmd != CMD_SLPIN) {
		sim_lcd_violation(SIM_LCD_V_CMD_AFTER_SLPOUT);
	}

	switch (cmd) {
	case CMD_NOP:
	case CMD_NORON:
	case CMD_CASET:
	case CMD_PASET:
	case CMD_MADCTL:
	case CMD_COLMOD:
		break;
	case CMD_SWRST:
		lcd_state_reset();
		break;
	case CMD_SLPIN:
		lcd.sleeping = true;
		break;
	case CMD_SLPOUT:
		lcd.sleeping = false;
		lcd.slpout_ms = now;
		break;
	case CMD_INVOFF:
		lcd.inverted = false;
		break;
	case CMD_INVON:
		lcd.inverted = true;
		break;
	case CMD_DISPOFF:
		lcd.display_on = false;
		break;
	case CMD_DISPON:
		if (lcd.sleeping) {
			sim_lcd_violation(SIM_LCD_V_RAMWR_ASLEEP);
		}
		lcd.display_on = true;
		break;
	case CMD_RAMWR:
		if (lcd.sleeping) {
			sim_lcd_violation(SIM_LCD_V_RAMWR_ASLEEP);
		}
		if (lcd.colmod != 0x05 && lcd.colmod != 0x06) {
			sim_lcd_violation(SIM_LCD_V_BAD_COLMOD);
		}
		lcd.cx = lcd.xs;
		lcd.cy = lcd.ys;
		break;
	default:
		sim_lcd_violation(SIM_LCD_V_UNKNOWN_CMD);
		lcd.cmd = CMD_NOP;
		break;
	}
}

static void window_param(uint16_t *start, uint16_t *end, uint16_t limit, uint8_t b)
{
	lcd.pbuf[lcd.nparam++] = b;
	if (lcd.nparam < 4) {
		return;
	}
	*start = (lcd.pbuf[0] << 8) | lcd.pbuf[1];
	*end = (lcd.pbuf[2] << 8) | lcd.pbuf[3];
	if (*start > *end || *end >= limit) {
		sim_lcd_violation(SIM_LCD_V_BAD_WINDOW);
	}
}

void sim_lcd_data(uint8_t b)
{
	lcd.data_bytes++;

	switch (lcd.cmd) {
	case CMD_CASET:
		if (lcd.nparam >= 4) {
			sim_lcd_violation(SIM_LCD_V_EXTRA_PARAM);
			return;
		}
		window_param(&lcd.xs, &lcd.xe, logical_w(), b);
		break;
	case CMD_PASET:
		if (lcd.nparam >= 4) {
			sim_lcd_violation(SIM_LCD_V_EXTRA_PARAM);
			return;
		}
		window_param(&lcd.ys, &lcd.ye, logical_h(), b);
		break;
	case CMD_MADCTL:
		if (lcd.nparam++ > 0) {
			sim_lcd_violation(SIM_LCD_V_EXTRA_PARAM);
			return;
		}
		lcd.madctl = b;
		break;
	case CMD_COLMOD:
		if (lcd.nparam++ > 0) {
			sim_lcd_violation(SIM_LCD_V_EXTRA_PARAM);
			return;
		}
		lcd.colmod = b & 7;
		break;
	case CMD_RAMWR:
		lcd.pix[lcd.npix++] = b;
		if (lcd.colmod == 0x05 && lcd.npix == 2) {
			put_pixel((lcd.pix[0] << 8) | lcd.pix[1]);
			lcd.npix = 0;
		} else if (lcd.colmod == 0x06 && lcd.npix == 3) {
			put_pixel(((lcd.pix[0] >> 1) << 11) | (lcd.pix[1] << 5) | (lcd.pix[2] >> 1));
			lcd.npix = 0;
		} else if (lcd.colmod != 0x05 && lcd.colmod != 0x06) {
			lcd.npix = 0;
		}
		break;
	default:
		sim_lcd_violation(SIM_LCD_V_STRAY_DATA);
		break;
	}
}

static uint8_t expand(uint8_t v, int bits)
{
	return (v << (8 - bits)) | (v >> (2 * bits - 8));
}

int sim_lcd_dump_png(const char *path)
{
	static uint8_t rgb[SIM_LCD_VISIBLE_W * SIM_LCD_VISIBLE_H * 3];

	for (int y = 0; y < SIM_LCD_VISIBLE_H; y++) {
		for (int x = 0; x < SIM_LCD_VISIBLE_W; x++) {
			uint16_t v = gram[x][GRAM_COLS - 1 - y];
			uint8_t *px = &rgb[(y * SIM_LCD_VISIBLE_W + x) * 3];

			if (lcd.sleeping || !lcd.display_on) {
				v = 0;
			} else if (lcd.inverted) {
				v = ~v;
			}
			px[0] = expand((v >> 11) & 0x1F, 5);
			px[1] = expand((v >> 5) & 0x3F, 6);
			px[2] = expand(v & 0x1F, 5);
		}
	}
	return sim_png_write_rgb(path, rgb, SIM_LCD_VISIBLE_W, SIM_LCD_VISIBLE_H);
}

static int cmd_shot(int argc, char **argv)
{
	if (argc < 2) {
		return -EINVAL;
	}
	return sim_lcd_dump_png(argv[1]);
}

static int sim_lcd_init(void)
{
	for (int r = 0; r < GRAM_ROWS; r++) {
		for (int c = 0; c < GRAM_COLS; c++) {
			gram[r][c] = GRAM_FILL;
		}
	}
	lcd_state_reset();
	sim_ctl_register("shot", cmd_shot);
	return 0;
}

SYS_INIT(sim_lcd_init, POST_KERNEL, 1);

static void sim_lcd_options(void)
{
	static struct args_struct_t opts[] = {
		{
			.option = "lcd-png",
			.name = "path",
			.type = 's',
			.dest = (void *)&png_path,
			.descript = "Write the LCD to this PNG file at exit",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(opts);
}

static void sim_lcd_exit(void)
{
	char line[96];

	if (png_path != NULL) {
		sim_lcd_dump_png(png_path);
	}
	if (lcd.cmds == 0) {
		return;
	}
	snprintf(line, sizeof(line), "lcd: %u commands, %u data bytes, %u pixels, %u violations\n",
		 lcd.cmds, lcd.data_bytes, lcd.pixels, sim_lcd_violation_total());
	sim_host_puts(line);
	for (int i = 0; i < SIM_LCD_V_COUNT; i++) {
		if (violation_counts[i] != 0) {
			snprintf(line, sizeof(line), "lcd:   %u x %s\n", violation_counts[i],
				 violation_text[i]);
			sim_host_puts(line);
		}
	}
}

NATIVE_TASK(sim_lcd_options, PRE_BOOT_1, 1);
NATIVE_TASK(sim_lcd_exit, ON_EXIT, 1);
