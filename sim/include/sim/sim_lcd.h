/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * HX8353E controller model and the protocol-violation counters shared with
 * the bus decoders that feed it.
 */

#ifndef DMRTASTIC_SIM_SIM_LCD_H_
#define DMRTASTIC_SIM_SIM_LCD_H_

#include <stdbool.h>
#include <stdint.h>

#define SIM_LCD_VISIBLE_W 160
#define SIM_LCD_VISIBLE_H 128

#define SIM_LCD_VIOLATIONS(X)                                                                      \
	X(WR_NO_CS, "WR strobe while CS inactive")                                                 \
	X(WR_IN_RESET, "WR strobe while RESET asserted")                                           \
	X(WR_RISE_CONCURRENT, "data, D/C or CS changed in the write that raised WR")               \
	X(DATA_WR_LOW, "data changed while WR low")                                                \
	X(DC_WR_LOW, "D/C changed while WR low")                                                   \
	X(CS_WR_LOW, "CS changed while WR low")                                                    \
	X(STRAY_RD, "RD strobe")                                                                   \
	X(UNKNOWN_CMD, "unknown command")                                                          \
	X(EXTRA_PARAM, "more parameter bytes than the command takes")                              \
	X(STRAY_DATA, "data byte with no command to receive it")                                   \
	X(CMD_AFTER_SLPOUT, "command within 120 ms of SLPOUT")                                     \
	X(RAMWR_ASLEEP, "RAMWR or DISPON while in sleep mode")                                     \
	X(BAD_COLMOD, "pixel format other than 16 bpp")                                            \
	X(WINDOW_OOB, "pixel outside the GRAM")                                                    \
	X(BAD_WINDOW, "window start beyond window end or outside the GRAM")                        \
	X(CMD_IN_RESET, "command while RESET asserted")

enum sim_lcd_violation {
#define X(id, text) SIM_LCD_V_##id,
	SIM_LCD_VIOLATIONS(X)
#undef X
		SIM_LCD_V_COUNT
};

void sim_lcd_violation(enum sim_lcd_violation v);
uint32_t sim_lcd_violation_count(enum sim_lcd_violation v);
uint32_t sim_lcd_violation_total(void);

void sim_lcd_hw_reset(bool asserted);
void sim_lcd_cmd(uint8_t cmd);
void sim_lcd_data(uint8_t data);

/** Write the visible window as a PNG. Returns 0 or a negative errno. */
int sim_lcd_dump_png(const char *path);

#endif /* DMRTASTIC_SIM_SIM_LCD_H_ */
