/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * AT1846S register addresses and bulk register-write tables, from the
 * AT1846S Programming Guide v1.4 (Auctus). 0xFF is a synthetic "delay"
 * entry for the table runner; 0x7F (bank select) is never cached.
 */

#ifndef DMRTASTIC_AT1846S_REGS_H_
#define DMRTASTIC_AT1846S_REGS_H_

#include <stdint.h>
#include <zephyr/kernel.h>

/* ---- Register addresses (Programming Guide v1.4) ----------------------- */

#define AT1846S_REG_CHIP_ID      0x00
#define AT1846S_REG_CLK_MODE     0x04
#define AT1846S_REG_FREQ_MODE    0x05
#define AT1846S_REG_CLK_CFG      0x0A
#define AT1846S_REG_UNK_0F       0x0F
#define AT1846S_REG_UNK_13       0x13
#define AT1846S_REG_IF_TUNING    0x15
#define AT1846S_REG_RSSI         0x1B /* signal:noise on read */
#define AT1846S_REG_CSS_FLAGS    0x1C
#define AT1846S_REG_GPIO_CFG     0x1F
#define AT1846S_REG_FREQ_HI      0x29
#define AT1846S_REG_FREQ_LO      0x2A
#define AT1846S_REG_CONTROL      0x30
#define AT1846S_REG_UNK_31       0x31
#define AT1846S_REG_AGC_TARGET   0x32
#define AT1846S_REG_AGC_NUMBER   0x33
#define AT1846S_REG_RX_DIG_GAIN  0x34
#define AT1846S_REG_TONE1        0x35
#define AT1846S_REG_VOICE_SEL    0x3A
#define AT1846S_REG_PK_DET_TH    0x3C
#define AT1846S_REG_RSSI3_TH     0x3F
#define AT1846S_REG_LOFREQ_AUDIO 0x40
#define AT1846S_REG_VOICE_GAIN   0x41
#define AT1846S_REG_VOX_SHUT     0x42
#define AT1846S_REG_FM_DEV       0x43
#define AT1846S_REG_VOL          0x44
#define AT1846S_REG_UNK_47       0x47
#define AT1846S_REG_NOISE1_TH    0x48
#define AT1846S_REG_SQ_THRESH    0x49
#define AT1846S_REG_CTCSS1       0x4A
#define AT1846S_REG_DCS_HI       0x4B
#define AT1846S_REG_DCS_LO       0x4C
#define AT1846S_REG_CTCSS2       0x4D
#define AT1846S_REG_CSS_EN       0x4E
#define AT1846S_REG_UNK_4F       0x4F
#define AT1846S_REG_UNK_53       0x53
#define AT1846S_REG_UNK_54       0x54
#define AT1846S_REG_UNK_55       0x55
#define AT1846S_REG_SQ_DET_TIME  0x56
#define AT1846S_REG_BYPASS_LPF   0x57
#define AT1846S_REG_FILTERS      0x58
#define AT1846S_REG_DEV_CFG      0x59
#define AT1846S_REG_UNK_5A       0x5A
#define AT1846S_REG_CTCSS_TH     0x5B
#define AT1846S_REG_NOISE2_TH    0x60
#define AT1846S_REG_MODU_DET_TH  0x62
#define AT1846S_REG_PRE_EMPH     0x63
#define AT1846S_REG_SIF_TH       0x65
#define AT1846S_REG_RSSI_COMP    0x66
#define AT1846S_REG_TONE_MODE    0x79
#define AT1846S_REG_BANK_SEL     0x7F /* never cached */

/* Synthetic register address used inside init tables for delay entries. */
#define AT1846S_REG_DELAY 0xFF

/* ---- REG_CONTROL (0x30) field encodings ------------------------------- */

/* High byte selects bandwidth. */
#define AT1846S_CTRL_HI_BW_12K5 0x60
#define AT1846S_CTRL_HI_BW_25K  0x70

/* Low byte selects mode. */
#define AT1846S_CTRL_LO_IDLE   0x06
#define AT1846S_CTRL_LO_RX_ON  0x26
#define AT1846S_CTRL_LO_FM_TX  0x46
#define AT1846S_CTRL_LO_DMR_TX 0xC6

/* ---- Init-table encoding ---------------------------------------------- */

struct at1846s_reg_entry {
	uint8_t reg;
	uint8_t hi;
	uint8_t lo;
};

#define AT1846S_DELAY_MS(ms)                                                                       \
	{                                                                                          \
		AT1846S_REG_DELAY, ((uint8_t)(((ms) >> 8) & 0xFF)), ((uint8_t)((ms)&0xFF))         \
	}

/* ---- Cold init: applied once at chip power-up ------------------------- */

static const struct at1846s_reg_entry at1846s_init_regs[] = {
	{0x30, 0x00, 0x01},                        /* soft reset */
	AT1846S_DELAY_MS(50),  {0x30, 0x00, 0x04}, /* power on */
	{0x04, 0x0F, 0xD0},                        /* clock mode (26 MHz xtal) */
	{0x0A, 0x7C, 0x20},    {0x13, 0xA1, 0x00}, {0x1F, 0x10, 0x01}, /* GPIO6 = squelch out,
									* GPIO1 = CSS out
									*/
	{0x31, 0x00, 0x31},    {0x33, 0x44, 0xA5},                     /* AGC number */
	{0x34, 0x2B, 0x89},                                            /* RX digital gain */
	{0x41, 0x41, 0x22},                                            /* digital voice gain */
	{0x42, 0x10, 0x52},                                            /* VOX shut threshold */
	{0x43, 0x01, 0x00},                                            /* FM deviation */
	{0x44, 0x07, 0xFF},                                            /* RX/TX gain (full) */
	{0x3A, 0x00, 0xC3},                                            /* SQL config */
	{0x59, 0x0B, 0x90},                                            /* deviation settings */
	{0x47, 0x7F, 0x2F},    {0x4F, 0x2C, 0x62}, {0x53, 0x00, 0x94}, {0x54, 0x2A, 0x3C},
	{0x55, 0x00, 0x81},    {0x56, 0x0B, 0x02}, /* SQ detect timing */
	{0x57, 0x1C, 0x00},                        /* bypass RSSI LPF */
	{0x58, 0x9C, 0xDD},                        /* filter custom setting */
	{0x5A, 0x06, 0xDB},    {0x63, 0x16, 0xAD}, /* pre-emphasis bypass threshold */
	{0x0F, 0x8A, 0x24},    {0x05, 0x87, 0x63}, {0x30, 0x40, 0xA4}, /* prepare for calibration */
	{0x30, 0x40, 0xA6},                                            /* enable calibration */
	AT1846S_DELAY_MS(100), {0x30, 0x40, 0x06},                     /* disable calibration */
	AT1846S_DELAY_MS(10),
};

/* ---- Post-init: applied once after the cold-init table ---------------- */

static const struct at1846s_reg_entry at1846s_postinit_regs[] = {
	{0x15, 0x11, 0x00}, /* IF tuning bits */
};

/* ---- 12.5 kHz bandwidth profile --------------------------------------- */

static const struct at1846s_reg_entry at1846s_bw_12k5_regs[] = {
	{0x3A, 0x44, 0xCB}, {0x15, 0x11, 0x00}, {0x32, 0x44, 0x95}, /* AGC target power */
	{0x3A, 0x00, 0xC3}, {0x59, 0x0B, 0x90}, {0x3F, 0x29, 0xD1}, {0x3C, 0x1B, 0x34},
	{0x48, 0x19, 0xB1}, {0x60, 0x0F, 0x17}, {0x62, 0x14, 0x25}, {0x65, 0x24, 0x94},
	{0x66, 0xEB, 0x2E}, {0x7F, 0x00, 0x01}, /* page 1 */
	{0x06, 0x00, 0x14}, {0x07, 0x02, 0x0C}, {0x08, 0x02, 0x14}, {0x09, 0x03, 0x0C},
	{0x0A, 0x03, 0x14}, {0x0B, 0x03, 0x24}, {0x0C, 0x03, 0x44}, {0x0D, 0x13, 0x44},
	{0x0E, 0x1B, 0x44}, {0x0F, 0x3F, 0x44}, {0x12, 0xE0, 0xEB}, {0x7F, 0x00, 0x00}, /* page 0 */
};

/* ---- 25 kHz bandwidth profile ----------------------------------------- */

static const struct at1846s_reg_entry at1846s_bw_25k_regs[] = {
	{0x3A, 0x40, 0xCB}, {0x15, 0x1F, 0x00}, {0x32, 0x75, 0x64}, {0x3A, 0x00, 0xC3},
	{0x59, 0x0B, 0xA0}, {0x3F, 0x29, 0xD1}, {0x3C, 0x1B, 0x34}, {0x48, 0x1E, 0x38},
	{0x60, 0x0F, 0x17}, {0x62, 0x37, 0x67}, {0x65, 0x24, 0x8A}, {0x66, 0xFF, 0x2E},
	{0x7F, 0x00, 0x01}, {0x06, 0x00, 0x24}, {0x07, 0x02, 0x14}, {0x08, 0x02, 0x24},
	{0x09, 0x03, 0x14}, {0x0A, 0x03, 0x24}, {0x0B, 0x03, 0x44}, {0x0C, 0x03, 0x84},
	{0x0D, 0x13, 0x84}, {0x0E, 0x1B, 0x84}, {0x0F, 0x3F, 0x84}, {0x12, 0xE0, 0xEB},
	{0x7F, 0x00, 0x00},
};

/* ---- FM mode profile -------------------------------------------------- */

static const struct at1846s_reg_entry at1846s_mode_fm_regs[] = {
	{0x33, 0x44, 0xA5}, {0x41, 0x44, 0x31}, {0x42, 0x10, 0xF0},
	{0x43, 0x00, 0xA9}, {0x58, 0xBC, 0x85}, /* FM filters; de-emphasis on AT1846S */
	{0x44, 0x06, 0xCC},                     /* internal volume 80% */
	{0x3A, 0x00, 0xC3}, {0x40, 0x00, 0x30}, /* low-freq audio path */
};

/* ---- DMR mode profile ------------------------------------------------- */

static const struct at1846s_reg_entry at1846s_mode_dmr_regs[] = {
	{0x40, 0x00, 0x31}, {0x15, 0x11, 0x00}, {0x32, 0x44, 0x95}, {0x3A, 0x00, 0xC3},
	{0x3C, 0x1B, 0x34}, {0x3F, 0x29, 0xD1}, {0x41, 0x41, 0x22}, {0x42, 0x10, 0x52},
	{0x43, 0x01, 0x00}, {0x48, 0x19, 0xB1}, {0x58, 0x9C, 0xDD}, {0x44, 0x07, 0xFF},
};

#endif /* DMRTASTIC_AT1846S_REGS_H_ */
