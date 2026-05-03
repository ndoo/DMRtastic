/*
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 * SPDX-License-Identifier: MIT
 *
 * HR-C6000 control-bus init tables and key register addresses. Paged
 * register space over SPI: writes are { page, reg, value[, value...] }.
 */

#ifndef DMRTASTIC_HR_C6000_REGS_H_
#define DMRTASTIC_HR_C6000_REGS_H_

#include <stdint.h>

/* ---- Control-bus pages ----------------------------------------------- */

#define HRC6000_PAGE_AUX     0x01
#define HRC6000_PAGE_LC      0x02
#define HRC6000_PAGE_AMBE    0x03  /* on the audio bus */
#define HRC6000_PAGE_CTRL    0x04

/* ---- A few well-known control-bus registers (page 0x04) -------------- */

#define HRC6000_REG_E0_CODEC      0xE0
#define HRC6000_REG_36_FM_FEED    0x36

#define HRC6000_E0_FM_RX          0x89  /* codec on, mic off (FM RX) */
#define HRC6000_E0_FM_TX          0xC9  /* codec on, mic on (FM TX / DMR TX) */

#define HRC6000_36_FM_FEED_ON     0x02  /* enable FM audio feedthrough (IF demod -> codec -> speaker amp) */
#define HRC6000_36_FM_FEED_OFF    0x00  /* disable feedthrough */

/* ---- Reg/value pair entry used by pair tables ------------------------ */

struct hr_c6000_reg_pair {
	uint8_t reg;
	uint8_t val;
};

/* ---- Init table 1: PLL clock setup (page 0x04) ----------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_pll_regs[] = {
	{ 0x0A, 0x80 },   /* internal clock */
	{ 0x0B, 0x28 },   /* PLL M register */
	{ 0x0C, 0x33 },   /* PLL dividers */
	{ 0x0A, 0x00 },   /* clock source = PLL */
	{ 0xB9, 0x05 },   /* system clock 1 */
	{ 0xBA, 0x04 },   /* system clock 2 */
	{ 0xBB, 0x02 },   /* system clock 3 */
};

/* ---- Init table 2: initial setup (page 0x04) ------------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_setup_regs[] = {
	{ 0xA1, 0x80 },   /* baseband receive mode */
	{ 0x10, 0xF3 },
	{ 0x5F, 0xF0 },   /* enable sync detect MS/BS/TDMA1/TDMA2 */
	{ 0x40, 0x43 },   /* DMR Rx, passive timing, normal mode */
	{ 0x07, 0x0B },   /* IF freq H (450 kHz default) */
	{ 0x08, 0xB8 },   /* IF freq M */
	{ 0x09, 0x00 },   /* IF freq L */
	{ 0x06, 0x21 },   /* SPI vocoder under MCU control */
	{ 0x00, 0xFF },   /* reset all modules */
	{ 0x01, 0xB0 },   /* 2-point mod, IF receive */
	{ 0x02, 0x00 },   /* TX I offset = 0 */
	{ 0x03, 0x00 },   /* RX I offset = 0 */
	{ 0x04, 0x00 },   /* TX Q offset = 0 */
	{ 0x05, 0x00 },   /* RX Q offset = 0 */
	{ 0x01, 0xF8 },
};

/* ---- Init table: 0x04 register burst at offset 0x11 (44 bytes) ------- */

static const uint8_t hr_c6000_init_ctrl_0x11_array[] = {
	0x80, 0x0C, 0x22, 0x01, 0x00, 0x00, 0x33, 0xEF,
	0x00, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0, 0x10, 0x00,
	0x00, 0x06, 0x3B, 0xF8, 0x0E, 0xFD, 0x40, 0xFF,
	0x00, 0x0B, 0x00, 0x00, 0x00, 0x06, 0x0B, 0x00,
	0x17, 0x02, 0xFF, 0xE0, 0x14, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

/* ---- Init table: vocoder open-music (page 0x04) ---------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_vocoder_regs[] = {
	{ 0x00, 0x2A },   /* partial reset */
	{ 0x06, 0x22 },   /* open music */
};

/* ---- Init table: post-flush register set (page 0x04) ----------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_post_flush_regs[] = {
	{ 0x06, 0x20 },   /* open music off */
	{ 0x14, 0xB0 },   /* DMR ID placeholder, low */
	{ 0x15, 0xDB },   /* DMR ID placeholder, mid */
	{ 0x16, 0x23 },   /* DMR ID placeholder, high */
};

/* ---- Init table: aux page-1 burst at 0x10 (32 bytes) ----------------- */

static const uint8_t hr_c6000_init_aux_0x10_array[] = {
	0x69, 0x69, 0x96, 0x96, 0x96, 0x99, 0x99, 0x99,
	0xA5, 0xA5, 0xAA, 0xAA, 0xCC, 0xCC, 0x00, 0xF0,
	0x01, 0xFF, 0x01, 0x0F, 0x00, 0x00, 0x00, 0x00,
	0x0D, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ---- Init table: aux page-1 burst at 0x30 (16 bytes) ----------------- */

static const uint8_t hr_c6000_init_aux_0x30_array[] = {
	0x00, 0x00, 0x20, 0x3C, 0xFF, 0xFF, 0x3F, 0x50,
	0x07, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ---- Init table: aux page-1 burst at 0x40 (7 bytes) ------------------ */

static const uint8_t hr_c6000_init_aux_0x40_array[] = {
	0x00, 0x01, 0x01, 0x02, 0x01, 0x1E, 0xF0,
};

/* ---- Init table: aux page-1 burst at 0x50 (5 bytes) ------------------ */

static const uint8_t hr_c6000_init_aux_0x50_array[] = {
	0x00, 0x08, 0xEB, 0x78, 0x67,
};

/* ---- Init table: more aux page-1 single writes ----------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_aux_more_regs[] = {
	{ 0x52, 0x08 },
	{ 0x53, 0xEB },
	{ 0x54, 0x78 },
	{ 0x45, 0x1E },
	{ 0x37, 0x50 },
	{ 0x35, 0xFF },
};

/* ---- Init table: late page-0x04 setup (codec + interrupts) ---------- */

static const struct hr_c6000_reg_pair hr_c6000_init_late_regs[] = {
	{ 0x39, 0x02 },
	{ 0x3D, 0x0A },
	{ 0x83, 0xFF },   /* clear interrupts */
	{ 0x87, 0x00 },   /* interrupt masks */
	{ 0x65, 0x0A },
	{ 0x1D, 0xFF },   /* unaddress mask */
	{ 0x1E, 0xF1 },   /* broadcast address */
	{ 0xE2, 0x06 },   /* configure codec */
	{ 0xE4, 0x27 },   /* mic gain */
	{ 0xE3, 0x52 },   /* codec defaults */
	{ 0xE5, 0x1A },
	{ 0xE1, 0x0F },
	{ 0xD1, 0xC4 },
	{ 0x25, 0x0E },   /* DAC power control */
	{ 0x26, 0xFD },   /* ADC control */
	{ 0x64, 0x00 },
	{ 0x10, 0x6B },   /* DMR Tier2, Slot1, Repeater, Aligned */
	{ 0x81, 0x19 },
	{ 0x01, 0xF0 },
	{ 0xE4, 0x27 },
	{ 0xE5, 0x1A },
	{ 0x37, 0x9E },
	{ 0xE0, 0xC9 },   /* codec under MCU control, lineout2 + mic_p */
	{ 0x25, 0x0E },
	{ 0x26, 0xFD },
	{ 0x48, 0x00 },   /* ref-osc calibration offsets — placeholder */
	{ 0x47, 0x21 },
	{ 0x1F, 0x10 },   /* CC1 = 1 */
};

/* ---- Init table: aux page-1 cleanup writes -------------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_aux_cleanup_regs[] = {
	{ 0x54, 0x78 },
	{ 0x24, 0x00 },
	{ 0x25, 0x00 },
	{ 0x26, 0x00 },
	{ 0x27, 0x00 },
};

/* ---- Init table: ready-to-receive (page 0x04) ----------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_rxprep_regs[] = {
	{ 0x41, 0x40 },   /* RX in next slot */
	{ 0x56, 0x00 },
	{ 0x5C, 0x09 },
	{ 0x5F, 0xF0 },
};

/* ---- MS sync pattern (aux page-1, burst at 0x04, 6 bytes) ----------- */

static const uint8_t hr_c6000_ms_sync_pattern[] = {
	0xD5, 0xD7, 0xF7, 0x7F, 0xD7, 0x57,
};

/* ---- Init table: final (page 0x04) ---------------------------------- */

static const struct hr_c6000_reg_pair hr_c6000_init_final_regs[] = {
	{ 0x11, 0x80 },   /* set local channel mode */
	{ 0x81, 0x19 },   /* interrupt masks */
	{ 0x85, 0x00 },   /* disable interrupts */
};

/* ---- FM RX mode: codec routing (page 0x04) -------------------------- */

static const struct hr_c6000_reg_pair hr_c6000_set_fm_rx_regs[] = {
	{ 0x60, 0x00 },   /* FM voice TX off */
	{ 0xE0, 0x89 },   /* codec on, mic off */
	{ 0x10, 0x80 },   /* FM mod mode */
	{ 0x34, 0x3C },   /* compressor off, de-emph on, 3 kHz audio filter */
	{ 0x81, 0x19 },   /* interrupt masks */
	{ 0x85, 0x00 },   /* disable interrupts */
	{ 0x26, 0xFD },   /* turns on FM receive */
};

#endif /* DMRTASTIC_HR_C6000_REGS_H_ */
