// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <sim/sim_host.h>
#include <sim/sim_png.h>

#define SIM_PNG_BUF_SIZE (256 * 1024)
#define STORED_BLOCK_MAX 65535U

static uint8_t png_buf[SIM_PNG_BUF_SIZE];

static uint32_t crc32_png(const uint8_t *data, size_t len, uint32_t crc)
{
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
		}
	}
	return crc;
}

static size_t put_chunk(uint8_t *out, const char *type, const uint8_t *data, size_t len)
{
	uint32_t crc;

	sys_put_be32(len, out);
	memcpy(out + 4, type, 4);
	if (len > 0 && data != out + 8) {
		memcpy(out + 8, data, len);
	}
	crc = crc32_png(out + 4, len + 4, 0xFFFFFFFFU) ^ 0xFFFFFFFFU;
	sys_put_be32(crc, out + 8 + len);
	return len + 12;
}

int sim_png_write_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	size_t raw_len = (size_t)(w * 3 + 1) * h;
	size_t nblocks = DIV_ROUND_UP(raw_len, STORED_BLOCK_MAX);
	size_t z_len = 2 + raw_len + nblocks * 5 + 4;
	size_t pos = 0;
	uint8_t ihdr[13];
	uint8_t *z;
	uint32_t a = 1;
	uint32_t b = 0;
	size_t zp = 0;
	size_t raw_pos = 0;

	if (8 + 25 + 12 + z_len + 12 > sizeof(png_buf)) {
		return -EFBIG;
	}

	memcpy(png_buf, sig, sizeof(sig));
	pos = sizeof(sig);

	sys_put_be32(w, &ihdr[0]);
	sys_put_be32(h, &ihdr[4]);
	ihdr[8] = 8;
	ihdr[9] = 2;
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;
	pos += put_chunk(&png_buf[pos], "IHDR", ihdr, sizeof(ihdr));

	/* The zlib stream is built in place as the IDAT payload. */
	z = &png_buf[pos + 8];
	z[zp++] = 0x78;
	z[zp++] = 0x01;
	while (raw_pos < raw_len) {
		size_t n = MIN(raw_len - raw_pos, STORED_BLOCK_MAX);

		z[zp++] = (raw_pos + n == raw_len) ? 1 : 0;
		z[zp++] = n & 0xFF;
		z[zp++] = n >> 8;
		z[zp++] = ~n & 0xFF;
		z[zp++] = (~n >> 8) & 0xFF;
		for (size_t i = 0; i < n; i++) {
			size_t p = raw_pos + i;
			uint8_t v = (p % (w * 3 + 1) == 0) ? 0 : rgb[(p / (w * 3 + 1)) * w * 3 +
								   (p % (w * 3 + 1)) - 1];

			z[zp++] = v;
			a = (a + v) % 65521U;
			b = (b + a) % 65521U;
		}
		raw_pos += n;
	}
	sys_put_be32((b << 16) | a, &z[zp]);
	zp += 4;

	pos += put_chunk(&png_buf[pos], "IDAT", &png_buf[pos + 8], zp);
	pos += put_chunk(&png_buf[pos], "IEND", NULL, 0);

	return sim_host_write_file(path, png_buf, pos) == 0 ? 0 : -EIO;
}
