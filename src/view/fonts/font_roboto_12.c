// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Andrew Yong <me@ndoo.sg>

/*******************************************************************************
 * Size: 12 px
 * Bpp: 2
 * Opts: --no-compress --no-prefilter --bpp 2 --size 12 --font
 * /Users/andrew/Development/DMRtastic/tools/.font_cache/roboto-wdth100-wght400.ttf
 *--autohint-strong -r 0x20-0x7E --format lvgl --lv-include lvgl.h --lv-font-name font_roboto_12 -o
 * /Users/andrew/Development/DMRtastic/src/view/fonts/font_roboto_12.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FONT_ROBOTO_12
#define FONT_ROBOTO_12 1
#endif

#if FONT_ROBOTO_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
	/* U+0020 " " */

	/* U+0021 "!" */
	0x30, 0xc3, 0xc, 0x30, 0xc0, 0x0, 0x30,

	/* U+0022 "\"" */
	0x3c, 0xf3, 0xc0,

	/* U+0023 "#" */
	0x3, 0x14, 0x3, 0x20, 0x3f, 0xfc, 0x5, 0x30, 0x9, 0x20, 0x7f, 0xfc, 0xc, 0x90, 0x8, 0x80,
	0x18, 0xc0,

	/* U+0024 "$" */
	0x3, 0x0, 0x30, 0x1f, 0xc3, 0x9e, 0x30, 0x73, 0x40, 0xa, 0x40, 0xa, 0x50, 0x37, 0xb, 0x2f,
	0xc0, 0x30, 0x0, 0x0,

	/* U+0025 "%" */
	0x1f, 0x40, 0xc, 0x72, 0x3, 0x1d, 0x80, 0x7d, 0x80, 0x0, 0x90, 0x0, 0x27, 0xd0, 0x23, 0x1c,
	0x4, 0xc7, 0x0, 0x1f, 0x40,

	/* U+0026 "&" */
	0x7, 0xd0, 0xc, 0x70, 0xc, 0x70, 0xa, 0xc0, 0xf, 0x40, 0x35, 0xcc, 0x30, 0xb8, 0x34, 0x74,
	0xf, 0xec,

	/* U+0027 "'" */
	0x33, 0x30,

	/* U+0028 "(" */
	0x0, 0x6, 0xc, 0x18, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30, 0x24, 0xc, 0x9, 0x2,

	/* U+0029 ")" */
	0x0, 0x30, 0xc, 0xd, 0x6, 0x7, 0x3, 0x3, 0x3, 0x6, 0x9, 0xc, 0x18, 0x20,

	/* U+002A "*" */
	0x3, 0x1, 0x31, 0x2f, 0xe0, 0xb4, 0x18, 0xc0, 0x0,

	/* U+002B "+" */
	0x3, 0x0, 0xc, 0x0, 0x30, 0x1f, 0xfd, 0x7, 0x0, 0xc, 0x0, 0x30, 0x0, 0x0,

	/* U+002C "," */
	0x33, 0x90,

	/* U+002D "-" */
	0xfc,

	/* U+002E "." */
	0x30,

	/* U+002F "/" */
	0x2, 0x40, 0xc0, 0x20, 0x24, 0xc, 0x6, 0x2, 0x40, 0xc0, 0x60, 0x20, 0x0,

	/* U+0030 "0" */
	0xf, 0xc3, 0x4a, 0x30, 0x33, 0x3, 0x30, 0x33, 0x3, 0x30, 0x33, 0x4a, 0xf, 0x80,

	/* U+0031 "1" */
	0x7, 0x3b, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,

	/* U+0032 "2" */
	0xf, 0xc0, 0xd2, 0xc2, 0x3, 0x0, 0x18, 0x0, 0xc0, 0xd, 0x0, 0x90, 0xa, 0x0, 0x3f, 0xf4,

	/* U+0033 "3" */
	0xf, 0xc3, 0x47, 0x10, 0x30, 0xa, 0x7, 0xc0, 0x6, 0x10, 0x33, 0x7, 0x1f, 0xc0,

	/* U+0034 "4" */
	0x0, 0xb0, 0x0, 0xf0, 0x3, 0x70, 0x6, 0x30, 0xc, 0x30, 0x24, 0x30, 0x3f, 0xfc, 0x0, 0x30,
	0x0, 0x30,

	/* U+0035 "5" */
	0x2f, 0xe0, 0xc0, 0x3, 0x0, 0xf, 0xe0, 0x10, 0xa0, 0x0, 0xc1, 0x3, 0xc, 0x28, 0x1f, 0x80,

	/* U+0036 "6" */
	0x7, 0x81, 0xc0, 0x34, 0x3, 0xbc, 0x34, 0xa3, 0x3, 0x30, 0x32, 0x8a, 0xf, 0x80,

	/* U+0037 "7" */
	0xbf, 0xf0, 0x6, 0x0, 0xc0, 0x1c, 0x2, 0x40, 0x30, 0xa, 0x0, 0xc0, 0x18, 0x0,

	/* U+0038 "8" */
	0x1f, 0xc3, 0x4b, 0x30, 0x33, 0x8a, 0x1f, 0xc3, 0x46, 0x30, 0x33, 0x46, 0x1f, 0xc0,

	/* U+0039 "9" */
	0xf, 0x83, 0x4a, 0x30, 0x33, 0x3, 0x34, 0xb0, 0xfb, 0x0, 0x60, 0xc, 0xf, 0x40,

	/* U+003A ":" */
	0x30, 0x0, 0x0, 0x30,

	/* U+003B ";" */
	0x30, 0x0, 0x3, 0x36, 0x0,

	/* U+003C "<" */
	0x1, 0x87, 0xdf, 0x42, 0xe0, 0xb, 0x80, 0x20,

	/* U+003D "=" */
	0xff, 0xc0, 0x0, 0x3, 0xff,

	/* U+003E ">" */
	0x90, 0x1f, 0x40, 0x6c, 0x2e, 0xb8, 0x20, 0x0,

	/* U+003F "?" */
	0x2f, 0x18, 0x70, 0xc, 0x6, 0x7, 0x3, 0x40, 0x80, 0x0, 0xc, 0x0,

	/* U+0040 "@" */
	0x1, 0xbf, 0x40, 0x1d, 0x2, 0x80, 0xc0, 0x3, 0x49, 0xf, 0x86, 0x30, 0xd3, 0xc, 0xc3, 0x8,
	0x33, 0x18, 0x20, 0xcc, 0x62, 0x8a, 0x34, 0xb7, 0xe0, 0x70, 0x0, 0x0, 0xe0, 0x0, 0x0, 0xbf,
	0x40,

	/* U+0041 "A" */
	0x2, 0xd0, 0x0, 0xf8, 0x0, 0x3b, 0x0, 0x2c, 0xd0, 0xe, 0x28, 0x3, 0x7, 0x2, 0xff, 0xd0,
	0xd0, 0x1c, 0x30, 0x3, 0x0,

	/* U+0042 "B" */
	0x3f, 0xc0, 0xc1, 0xc3, 0x3, 0xc, 0x28, 0x3f, 0xd0, 0xc0, 0xa3, 0x0, 0xcc, 0xa, 0x3f, 0xe0,

	/* U+0043 "C" */
	0x7, 0xf0, 0x1c, 0x1c, 0x30, 0xd, 0x30, 0x0, 0x30, 0x0, 0x30, 0x0, 0x30, 0xd, 0x1c, 0x1c,
	0x7, 0xf0,

	/* U+0044 "D" */
	0x3f, 0xd0, 0xc1, 0xc3, 0x1, 0x8c, 0x3, 0x30, 0xc, 0xc0, 0x33, 0x1, 0x8c, 0x1c, 0x3f, 0xc0,

	/* U+0045 "E" */
	0x3f, 0xf0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x3f, 0xe0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x3f, 0xf0,

	/* U+0046 "F" */
	0x3f, 0xf0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x3f, 0xe0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x30, 0x0,

	/* U+0047 "G" */
	0xb, 0xe0, 0xa0, 0xa3, 0x0, 0x4c, 0x0, 0x30, 0xfc, 0xc0, 0x33, 0x0, 0xc7, 0x7, 0xb, 0xf0,

	/* U+0048 "H" */
	0x30, 0xc, 0xc0, 0x33, 0x0, 0xcc, 0x3, 0x3f, 0xfc, 0xc0, 0x33, 0x0, 0xcc, 0x3, 0x30, 0xc,

	/* U+0049 "I" */
	0x33, 0x33, 0x33, 0x33, 0x30,

	/* U+004A "J" */
	0x0, 0x30, 0x3, 0x0, 0x30, 0x3, 0x0, 0x30, 0x3, 0x50, 0x37, 0xa, 0x2f, 0x80,

	/* U+004B "K" */
	0x30, 0x28, 0x30, 0xb0, 0x31, 0xc0, 0x37, 0x0, 0x3f, 0x0, 0x36, 0xc0, 0x30, 0xe0, 0x30,
	0x34, 0x30, 0x1c,

	/* U+004C "L" */
	0x30, 0x3, 0x0, 0x30, 0x3, 0x0, 0x30, 0x3, 0x0, 0x30, 0x3, 0x0, 0x3f, 0xf0,

	/* U+004D "M" */
	0x38, 0x2, 0xcf, 0x0, 0xf3, 0xc0, 0x7c, 0xd8, 0x27, 0x33, 0xc, 0xcc, 0xd6, 0x33, 0x1b, 0xc,
	0xc3, 0xc3, 0x30, 0x90, 0xc0,

	/* U+004E "N" */
	0x30, 0xc, 0xf0, 0x33, 0xd0, 0xcc, 0xc3, 0x32, 0x4c, 0xc3, 0x33, 0xa, 0xcc, 0xf, 0x30, 0x1c,

	/* U+004F "O" */
	0x7, 0xf4, 0x1c, 0x1c, 0x34, 0x6, 0x30, 0x3, 0x30, 0x3, 0x30, 0x3, 0x34, 0x6, 0x1c, 0x1c,
	0x7, 0xf4,

	/* U+0050 "P" */
	0x3f, 0xe0, 0xc0, 0xa3, 0x0, 0xcc, 0xa, 0x3f, 0xe0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x30, 0x0,

	/* U+0051 "Q" */
	0x7, 0xf4, 0x1c, 0x1c, 0x34, 0x6, 0x30, 0x3, 0x30, 0x3, 0x30, 0x3, 0x34, 0x6, 0x1c, 0x1c,
	0x7, 0xf8, 0x0, 0xe, 0x0, 0x0,

	/* U+0052 "R" */
	0x3f, 0xe0, 0x30, 0x28, 0x30, 0xc, 0x30, 0x28, 0x3f, 0xe0, 0x30, 0xa0, 0x30, 0x30, 0x30,
	0x28, 0x30, 0xc,

	/* U+0053 "S" */
	0xf, 0xe0, 0xd0, 0xa3, 0x0, 0x8a, 0x0, 0x7, 0xd0, 0x0, 0xa5, 0x0, 0xdd, 0xa, 0x1f, 0xe0,

	/* U+0054 "T" */
	0xff, 0xf8, 0xc, 0x0, 0x30, 0x0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x30, 0x0, 0xc0, 0x3, 0x0,

	/* U+0055 "U" */
	0x30, 0xc, 0xc0, 0x33, 0x0, 0xcc, 0x3, 0x30, 0xc, 0xc0, 0x33, 0x0, 0xce, 0xa, 0xb, 0xe0,

	/* U+0056 "V" */
	0xa0, 0xd, 0x70, 0xc, 0x30, 0x28, 0x28, 0x34, 0x1c, 0x30, 0xc, 0xa0, 0xa, 0xc0, 0x3, 0xc0,
	0x3, 0x80,

	/* U+0057 "W" */
	0xa0, 0x30, 0x35, 0xc1, 0xd0, 0xc3, 0xe, 0x87, 0xd, 0x33, 0x18, 0x25, 0x8c, 0x90, 0x69,
	0x27, 0x0, 0xf0, 0x6c, 0x3, 0xc0, 0xe0, 0xa, 0x3, 0x40,

	/* U+0058 "X" */
	0x70, 0x1c, 0x28, 0x30, 0xc, 0xa0, 0x7, 0xc0, 0x3, 0x80, 0xb, 0xc0, 0xc, 0xa0, 0x28, 0x34,
	0x70, 0x1c,

	/* U+0059 "Y" */
	0x34, 0xa, 0x1c, 0xc, 0xd, 0x28, 0x3, 0x30, 0x2, 0xe0, 0x0, 0xc0, 0x0, 0xc0, 0x0, 0xc0, 0x0,
	0xc0,

	/* U+005A "Z" */
	0xff, 0xf0, 0x6, 0x0, 0xc0, 0x20, 0x5, 0x0, 0x80, 0x30, 0x9, 0x0, 0xff, 0xf0,

	/* U+005B "[" */
	0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c,

	/* U+005C "\\" */
	0x90, 0xc, 0x3, 0x0, 0xa0, 0xc, 0x3, 0x0, 0x60, 0xc, 0x2, 0x40, 0x60,

	/* U+005D "]" */
	0x3c, 0x30, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc3, 0x3c,

	/* U+005E "^" */
	0x4, 0x7, 0x42, 0xa0, 0xcc, 0x62, 0x40,

	/* U+005F "_" */
	0xff, 0xd0,

	/* U+0060 "`" */
	0x70, 0x90,

	/* U+0061 "a" */
	0xf, 0xc0, 0xd1, 0xc0, 0x3, 0x7, 0xfc, 0x34, 0x30, 0xc2, 0xc1, 0xfb, 0x0,

	/* U+0062 "b" */
	0x30, 0x3, 0x0, 0x30, 0x3, 0xbc, 0x34, 0xa3, 0x3, 0x30, 0x33, 0x3, 0x34, 0xa3, 0xbc,

	/* U+0063 "c" */
	0xb, 0xd0, 0xa0, 0xc3, 0x1, 0xc, 0x0, 0x30, 0x0, 0xa0, 0xc0, 0xbd, 0x0,

	/* U+0064 "d" */
	0x0, 0x30, 0x3, 0x0, 0x30, 0xfb, 0x38, 0x73, 0x3, 0x30, 0x33, 0x3, 0x28, 0x70, 0xfb,

	/* U+0065 "e" */
	0xb, 0xd0, 0xa1, 0xc3, 0x3, 0x4f, 0xfd, 0x30, 0x0, 0xa0, 0x80, 0xbe, 0x0,

	/* U+0066 "f" */
	0x7, 0x83, 0x0, 0xc0, 0xfd, 0xc, 0x3, 0x0, 0xc0, 0x30, 0xc, 0x3, 0x0,

	/* U+0067 "g" */
	0xf, 0xb3, 0x87, 0x30, 0x33, 0x3, 0x30, 0x32, 0x87, 0xf, 0xb0, 0x3, 0x24, 0xa0, 0xf8,

	/* U+0068 "h" */
	0x30, 0x3, 0x0, 0x30, 0x3, 0xbd, 0x34, 0x73, 0x3, 0x30, 0x33, 0x3, 0x30, 0x33, 0x3,

	/* U+0069 "i" */
	0x30, 0x3, 0xc, 0x30, 0xc3, 0xc, 0x30,

	/* U+006A "j" */
	0xc, 0x0, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0x74,

	/* U+006B "k" */
	0x30, 0x0, 0xc0, 0x3, 0x0, 0xc, 0x34, 0x33, 0x80, 0xe8, 0x3, 0xe0, 0xc, 0xd0, 0x30, 0xc0,
	0xc2, 0xc0,

	/* U+006C "l" */
	0x33, 0x33, 0x33, 0x33, 0x33,

	/* U+006D "m" */
	0x3b, 0xdb, 0xd3, 0x47, 0x47, 0x30, 0x30, 0x33, 0x3, 0x3, 0x30, 0x30, 0x33, 0x3, 0x3, 0x30,
	0x30, 0x30,

	/* U+006E "n" */
	0x3b, 0xd3, 0x47, 0x30, 0x33, 0x3, 0x30, 0x33, 0x3, 0x30, 0x30,

	/* U+006F "o" */
	0xb, 0xe0, 0xa0, 0xd3, 0x1, 0xcc, 0x3, 0x30, 0x1c, 0xa0, 0xd0, 0xbe, 0x0,

	/* U+0070 "p" */
	0x3b, 0xc3, 0x4a, 0x30, 0x33, 0x3, 0x30, 0x33, 0x4a, 0x3b, 0xc3, 0x0, 0x30, 0x3, 0x0,

	/* U+0071 "q" */
	0xf, 0xb3, 0x87, 0x30, 0x33, 0x3, 0x30, 0x33, 0x87, 0xf, 0xb0, 0x3, 0x0, 0x30, 0x3,

	/* U+0072 "r" */
	0x3b, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30,

	/* U+0073 "s" */
	0x7f, 0x34, 0x7e, 0x0, 0xbc, 0x1, 0xf4, 0x73, 0xf4,

	/* U+0074 "t" */
	0x4, 0x3, 0x7, 0xf0, 0x30, 0xc, 0x3, 0x0, 0xc0, 0x30, 0xb, 0x0,

	/* U+0075 "u" */
	0x30, 0x33, 0x3, 0x30, 0x33, 0x3, 0x30, 0x33, 0xb, 0x1f, 0xb0,

	/* U+0076 "v" */
	0x90, 0x97, 0xc, 0x31, 0xc2, 0x64, 0x1b, 0x0, 0xf0, 0x9, 0x0,

	/* U+0077 "w" */
	0x90, 0xc1, 0x98, 0x74, 0x93, 0x2a, 0x30, 0xcc, 0xcc, 0x2a, 0x2a, 0x7, 0x47, 0x40, 0xc0,
	0xc0,

	/* U+0078 "x" */
	0x70, 0xd3, 0x58, 0xf, 0x0, 0xe0, 0x1f, 0x3, 0x5c, 0x70, 0xd0,

	/* U+0079 "y" */
	0x90, 0xd7, 0xc, 0x31, 0xc3, 0x64, 0x1b, 0x0, 0xf0, 0xe, 0x0, 0xc0, 0x1c, 0x7, 0x40,

	/* U+007A "z" */
	0xff, 0xc0, 0x38, 0x7, 0x0, 0xc0, 0x34, 0xa, 0x0, 0xff, 0xc0,

	/* U+007B "{" */
	0x2, 0x2, 0x40, 0xc0, 0x30, 0xc, 0x7, 0x7, 0x40, 0x70, 0xc, 0x3, 0x0, 0xc0, 0x34, 0x2, 0x0,

	/* U+007C "|" */
	0x33, 0x33, 0x33, 0x33, 0x33, 0x30,

	/* U+007D "}" */
	0x0, 0xc, 0x1, 0x80, 0x30, 0xc, 0x3, 0x0, 0xe0, 0x2c, 0xc, 0x3, 0x0, 0xc0, 0x30, 0x28, 0x4,
	0x0,

	/* U+007E "~" */
	0x2f, 0x49, 0x21, 0xf8};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
	{.bitmap_index = 0,
	 .adv_w = 0,
	 .box_w = 0,
	 .box_h = 0,
	 .ofs_x = 0,
	 .ofs_y = 0} /* id = 0 reserved */,
	{.bitmap_index = 0, .adv_w = 48, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 0, .adv_w = 50, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 7, .adv_w = 62, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 7},
	{.bitmap_index = 10, .adv_w = 118, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 28, .adv_w = 108, .box_w = 6, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
	{.bitmap_index = 48, .adv_w = 141, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 69, .adv_w = 119, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 87, .adv_w = 34, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 7},
	{.bitmap_index = 89, .adv_w = 66, .box_w = 4, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 103, .adv_w = 67, .box_w = 4, .box_h = 14, .ofs_x = -1, .ofs_y = -3},
	{.bitmap_index = 117, .adv_w = 83, .box_w = 6, .box_h = 6, .ofs_x = -1, .ofs_y = 3},
	{.bitmap_index = 126, .adv_w = 109, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 140, .adv_w = 38, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 142, .adv_w = 53, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
	{.bitmap_index = 143, .adv_w = 51, .box_w = 2, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 144, .adv_w = 79, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
	{.bitmap_index = 157, .adv_w = 108, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 171, .adv_w = 108, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 180, .adv_w = 108, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 196, .adv_w = 108, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 210, .adv_w = 108, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 228, .adv_w = 108, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 244, .adv_w = 108, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 258, .adv_w = 108, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 272, .adv_w = 108, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 286, .adv_w = 108, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 300, .adv_w = 47, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 304, .adv_w = 41, .box_w = 2, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
	{.bitmap_index = 309, .adv_w = 98, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 1},
	{.bitmap_index = 317, .adv_w = 105, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
	{.bitmap_index = 322, .adv_w = 100, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 1},
	{.bitmap_index = 330, .adv_w = 91, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 342, .adv_w = 172, .box_w = 11, .box_h = 12, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 375, .adv_w = 125, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 396, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 412, .adv_w = 125, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 430, .adv_w = 126, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 446, .adv_w = 109, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 462, .adv_w = 106, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 478, .adv_w = 131, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 494, .adv_w = 137, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 510, .adv_w = 52, .box_w = 2, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 515, .adv_w = 106, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 529, .adv_w = 120, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 547, .adv_w = 103, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 561, .adv_w = 168, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 582, .adv_w = 137, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 598, .adv_w = 132, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 616, .adv_w = 121, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 632, .adv_w = 132, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
	{.bitmap_index = 654, .adv_w = 118, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 672, .adv_w = 114, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 688, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 704, .adv_w = 125, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 720, .adv_w = 122, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 738, .adv_w = 170, .box_w = 11, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 763, .adv_w = 120, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 781, .adv_w = 115, .box_w = 8, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
	{.bitmap_index = 799, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
	{.bitmap_index = 813, .adv_w = 51, .box_w = 4, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
	{.bitmap_index = 826, .adv_w = 79, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
	{.bitmap_index = 839, .adv_w = 51, .box_w = 3, .box_h = 13, .ofs_x = -1, .ofs_y = -2},
	{.bitmap_index = 849, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
	{.bitmap_index = 856, .adv_w = 87, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
	{.bitmap_index = 858, .adv_w = 59, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 8},
	{.bitmap_index = 860, .adv_w = 104, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 873, .adv_w = 108, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 888, .adv_w = 101, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 901, .adv_w = 108, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 916, .adv_w = 102, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 929, .adv_w = 67, .box_w = 5, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
	{.bitmap_index = 942, .adv_w = 108, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 957, .adv_w = 106, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 972, .adv_w = 47, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 979, .adv_w = 46, .box_w = 4, .box_h = 12, .ofs_x = -1, .ofs_y = -3},
	{.bitmap_index = 991, .adv_w = 97, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1009, .adv_w = 47, .box_w = 2, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1014, .adv_w = 168, .box_w = 10, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1032, .adv_w = 106, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1043, .adv_w = 110, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1056, .adv_w = 108, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 1071, .adv_w = 109, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 1086, .adv_w = 65, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1093, .adv_w = 99, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
	{.bitmap_index = 1102, .adv_w = 63, .box_w = 5, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
	{.bitmap_index = 1114, .adv_w = 106, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1125, .adv_w = 93, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1136, .adv_w = 144, .box_w = 9, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1152, .adv_w = 95, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
	{.bitmap_index = 1163, .adv_w = 91, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 1178, .adv_w = 95, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
	{.bitmap_index = 1189, .adv_w = 65, .box_w = 5, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
	{.bitmap_index = 1206, .adv_w = 47, .box_w = 2, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
	{.bitmap_index = 1212, .adv_w = 65, .box_w = 5, .box_h = 14, .ofs_x = -1, .ofs_y = -3},
	{.bitmap_index = 1230, .adv_w = 131, .box_w = 8, .box_h = 2, .ofs_x = 0, .ofs_y = 3}};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] = {{.range_start = 32,
						.range_length = 95,
						.glyph_id_start = 1,
						.unicode_list = NULL,
						.glyph_id_ofs_list = NULL,
						.list_length = 0,
						.type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}};

/*-----------------
 *    KERNING
 *----------------*/

/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] = {
	1,  53, 3,  3,  3,  8,  8,  3,  8,  8,  9,  55, 9,  56, 9,  58, 13, 3,  13, 8,  15, 3,  15,
	8,  16, 16, 34, 3,  34, 8,  34, 32, 34, 36, 34, 40, 34, 48, 34, 50, 34, 53, 34, 54, 34, 55,
	34, 56, 34, 58, 34, 80, 34, 85, 34, 86, 34, 87, 34, 88, 34, 90, 34, 91, 35, 53, 35, 55, 35,
	58, 36, 10, 36, 53, 36, 62, 36, 94, 37, 13, 37, 15, 37, 34, 37, 53, 37, 55, 37, 57, 37, 58,
	37, 59, 38, 53, 38, 68, 38, 69, 38, 70, 38, 71, 38, 72, 38, 80, 38, 82, 38, 86, 38, 87, 38,
	88, 38, 90, 39, 13, 39, 15, 39, 34, 39, 43, 39, 53, 39, 66, 39, 68, 39, 69, 39, 70, 39, 72,
	39, 80, 39, 82, 39, 83, 39, 86, 39, 87, 39, 90, 41, 34, 41, 53, 41, 57, 41, 58, 42, 34, 42,
	53, 42, 57, 42, 58, 43, 34, 44, 14, 44, 36, 44, 40, 44, 48, 44, 50, 44, 68, 44, 69, 44, 70,
	44, 72, 44, 78, 44, 79, 44, 80, 44, 81, 44, 82, 44, 86, 44, 87, 44, 88, 44, 90, 45, 34, 45,
	36, 45, 40, 45, 48, 45, 50, 45, 53, 45, 54, 45, 55, 45, 56, 45, 58, 45, 86, 45, 87, 45, 88,
	45, 90, 46, 34, 46, 53, 46, 57, 46, 58, 47, 34, 47, 53, 47, 57, 47, 58, 48, 13, 48, 15, 48,
	34, 48, 53, 48, 55, 48, 57, 48, 58, 48, 59, 49, 13, 49, 15, 49, 34, 49, 43, 49, 57, 49, 59,
	49, 66, 49, 68, 49, 69, 49, 70, 49, 72, 49, 80, 49, 82, 49, 85, 49, 87, 49, 90, 50, 53, 50,
	55, 50, 56, 50, 58, 51, 53, 51, 55, 51, 58, 53, 1,  53, 13, 53, 14, 53, 15, 53, 34, 53, 36,
	53, 40, 53, 43, 53, 48, 53, 50, 53, 52, 53, 53, 53, 55, 53, 56, 53, 58, 53, 66, 53, 68, 53,
	69, 53, 70, 53, 72, 53, 78, 53, 79, 53, 80, 53, 81, 53, 82, 53, 83, 53, 84, 53, 86, 53, 87,
	53, 88, 53, 89, 53, 90, 53, 91, 54, 34, 55, 10, 55, 13, 55, 14, 55, 15, 55, 34, 55, 36, 55,
	40, 55, 48, 55, 50, 55, 62, 55, 66, 55, 68, 55, 69, 55, 70, 55, 72, 55, 80, 55, 82, 55, 83,
	55, 86, 55, 87, 55, 90, 55, 94, 56, 10, 56, 13, 56, 14, 56, 15, 56, 34, 56, 53, 56, 62, 56,
	66, 56, 68, 56, 69, 56, 70, 56, 72, 56, 80, 56, 82, 56, 83, 56, 86, 56, 94, 57, 14, 57, 36,
	57, 40, 57, 48, 57, 50, 57, 55, 57, 68, 57, 69, 57, 70, 57, 72, 57, 80, 57, 82, 57, 86, 57,
	87, 57, 90, 58, 7,  58, 10, 58, 11, 58, 13, 58, 14, 58, 15, 58, 34, 58, 36, 58, 40, 58, 43,
	58, 48, 58, 50, 58, 52, 58, 53, 58, 54, 58, 55, 58, 56, 58, 57, 58, 58, 58, 62, 58, 66, 58,
	68, 58, 69, 58, 70, 58, 71, 58, 72, 58, 78, 58, 79, 58, 80, 58, 81, 58, 82, 58, 83, 58, 84,
	58, 85, 58, 86, 58, 87, 58, 89, 58, 90, 58, 91, 58, 94, 59, 34, 59, 36, 59, 40, 59, 48, 59,
	50, 59, 68, 59, 69, 59, 70, 59, 72, 59, 80, 59, 82, 59, 86, 59, 87, 59, 88, 59, 90, 60, 43,
	60, 54, 66, 3,  66, 8,  66, 87, 66, 90, 67, 3,  67, 8,  67, 87, 67, 89, 67, 90, 67, 91, 68,
	3,  68, 8,  70, 3,  70, 8,  70, 87, 70, 90, 71, 3,  71, 8,  71, 10, 71, 62, 71, 68, 71, 69,
	71, 70, 71, 72, 71, 82, 71, 94, 73, 3,  73, 8,  76, 68, 76, 69, 76, 70, 76, 72, 76, 82, 78,
	3,  78, 8,  79, 3,  79, 8,  80, 3,  80, 8,  80, 87, 80, 89, 80, 90, 80, 91, 81, 3,  81, 8,
	81, 87, 81, 89, 81, 90, 81, 91, 83, 3,  83, 8,  83, 13, 83, 15, 83, 66, 83, 68, 83, 69, 83,
	70, 83, 71, 83, 72, 83, 80, 83, 82, 83, 85, 83, 87, 83, 88, 83, 90, 85, 80, 87, 3,  87, 8,
	87, 13, 87, 15, 87, 66, 87, 68, 87, 69, 87, 70, 87, 71, 87, 72, 87, 80, 87, 82, 88, 13, 88,
	15, 89, 68, 89, 69, 89, 70, 89, 72, 89, 80, 89, 82, 90, 3,  90, 8,  90, 13, 90, 15, 90, 66,
	90, 68, 90, 69, 90, 70, 90, 71, 90, 72, 90, 80, 90, 82, 91, 68, 91, 69, 91, 70, 91, 72, 91,
	80, 91, 82, 92, 43, 92, 54};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] = {
	-4,  -10, -10, -10, -10, 2,   2,   2,   -16, -16, -16, -16, -21, -11, -11, -6, -1,  -1,
	-1,  -1,  -12, -2,  -8,  -6,  -9,  -1,  -2,  -1,  -5,  -3,  -5,  1,   -3,  -2, -5,  -2,
	-3,  -1,  -2,  -10, -10, -2,  -3,  -2,  -2,  -4,  -2,  2,   -2,  -2,  -2,  -2, -2,  -2,
	-2,  -2,  -2,  -2,  -2,  -22, -22, -16, -25, 2,   -3,  -2,  -2,  -2,  -2,  -2, -2,  -2,
	-2,  -2,  -2,  2,   -3,  2,   -3,  2,   -3,  2,   -3,  -2,  -6,  -3,  -3,  -3, -3,  -2,
	-2,  -2,  -2,  -2,  -2,  -3,  -2,  -2,  -2,  -4,  -6,  -4,  2,   -6,  -6,  -6, -6,  -26,
	-5,  -16, -13, -22, -4,  -12, -9,  -12, 2,   -3,  2,   -3,  2,   -3,  2,   -3, -10, -10,
	-2,  -3,  -2,  -2,  -4,  -2,  -30, -30, -13, -19, -3,  -2,  -1,  -1,  -1,  -1, -1,  -1,
	-1,  1,   1,   1,   -4,  -3,  -2,  -3,  -7,  -2,  -4,  -4,  -20, -22, -20, -7, -3,  -3,
	-22, -3,  -3,  -1,  2,   2,   1,   2,   -11, -9,  -9,  -9,  -9,  -10, -10, -9, -10, -9,
	-7,  -11, -9,  -7,  -5,  -7,  -7,  -6,  -2,  2,   -21, -3,  -21, -7,  -1,  -1, -1,  -1,
	2,   -4,  -4,  -4,  -4,  -4,  -4,  -4,  -3,  -3,  -1,  -1,  2,   1,   -12, -6, -12, -4,
	1,   1,   -3,  -3,  -3,  -3,  -3,  -3,  -3,  -2,  -2,  1,   -4,  -2,  -2,  -2, -2,  1,
	-2,  -2,  -2,  -2,  -2,  -2,  -2,  -3,  -3,  -3,  2,   -5,  -20, -5,  -20, -9, -3,  -3,
	-9,  -3,  -3,  -1,  2,   -9,  2,   2,   1,   2,   2,   -7,  -6,  -6,  -6,  -2, -6,  -4,
	-4,  -6,  -4,  -6,  -4,  -5,  -2,  -4,  -2,  -2,  -2,  -3,  2,   1,   -2,  -2, -2,  -2,
	-2,  -2,  -2,  -2,  -2,  -2,  -2,  -3,  -3,  -3,  -2,  -2,  -6,  -6,  -1,  -1, -3,  -3,
	-1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  2,   2,   2,   2,   -2,  -2, -2,  -2,
	-2,  2,   -10, -10, -2,  -2,  -2,  -2,  -2,  -10, -10, -10, -10, -13, -13, -1, -2,  -1,
	-1,  -3,  -3,  -1,  -1,  -1,  -1,  2,   2,   -12, -12, -4,  -2,  -2,  -2,  1,  -2,  -2,
	-2,  5,   2,   2,   2,   -2,  1,   1,   -10, -10, -1,  -1,  -1,  -1,  1,   -1, -1,  -1,
	-12, -12, -2,  -2,  -2,  -2,  -2,  -2,  1,   1,   -10, -10, -1,  -1,  -1,  -1, 1,   -1,
	-1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -2,  -2};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs = {.glyph_ids = kern_pair_glyph_ids,
						       .values = kern_pair_values,
						       .pair_cnt = 406,
						       .glyph_ids_size = 0};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
	.glyph_bitmap = glyph_bitmap,
	.glyph_dsc = glyph_dsc,
	.cmaps = cmaps,
	.kern_dsc = &kern_pairs,
	.kern_scale = 16,
	.cmap_num = 1,
	.bpp = 2,
	.kern_classes = 0,
	.bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
	.cache = &cache
#endif
};

/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_roboto_12 = {
#else
lv_font_t font_roboto_12 = {
#endif
	.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, /*Function pointer to get glyph's data*/
	.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt, /*Function pointer to get glyph's bitmap*/
	.line_height = 14, /*The maximum line height required by the font*/
	.base_line = 3,    /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
	.subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
	.underline_position = -1,
	.underline_thickness = 1,
#endif
	.dsc = &font_dsc, /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
	.fallback = NULL,
#endif
	.user_data = NULL,
};

#endif /*#if FONT_ROBOTO_12*/
