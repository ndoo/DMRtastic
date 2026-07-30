// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "codeplug.h"

LOG_MODULE_REGISTER(codeplug, LOG_LEVEL_INF);

#define CP_MAP DT_NODELABEL(codeplug_map)

#define CP_OFFSET(prop) DT_PROP_BY_IDX(CP_MAP, prop, 0)
#define CP_SIZE(prop)   DT_PROP_BY_IDX(CP_MAP, prop, 1)

static const struct device *const cp_flash_dev =
	DEVICE_DT_GET(DT_PHANDLE(CP_MAP, codeplug_flash));

int codeplug_raw_read(off_t offset, void *buf, size_t len)
{
	if (!device_is_ready(cp_flash_dev)) {
		return -ENODEV;
	}
	return flash_read(cp_flash_dev, offset, buf, len);
}

int codeplug_write(off_t offset, const void *buf, size_t len)
{
	ARG_UNUSED(buf);
	LOG_INF("codeplug_write: offset=0x%lx len=%zu -- write path not implemented, no-op",
		(unsigned long)offset, len);
	return -ENOTSUP;
}

int codeplug_get_jedec_id(uint8_t id[3])
{
	if (!device_is_ready(cp_flash_dev)) {
		return -ENODEV;
	}
	return flash_read_jedec_id(cp_flash_dev, id);
}

/* ---- BCD field decoding -------------------------------------------------- */

/* rxFreq/txFreq (deca-Hz), contact tgNumber, and device-info's freq range are stored BCD-encoded on flash. */
static uint32_t cp_bcd_to_uint32(uint32_t bcd)
{
	uint32_t result = 0;
	uint32_t mult = 1;

	while (bcd) {
		result += (bcd & 0xF) * mult;
		mult *= 10;
		bcd >>= 4;
	}
	return result;
}

static uint32_t cp_byteswap32(uint32_t n)
{
	return ((n & 0x000000FFU) << 24) | ((n & 0x0000FF00U) << 8) |
	       ((n & 0x00FF0000U) >> 8) | ((n & 0xFF000000U) >> 24);
}

static void cp_decode_channel_freqs(struct cp_channel *ch)
{
	ch->rxFreq = cp_bcd_to_uint32(ch->rxFreq) * 10;
	ch->txFreq = cp_bcd_to_uint32(ch->txFreq) * 10;
}

/* Unlike channel freq, device-info's band-limit BCD decodes to a plain whole-MHz integer, not x10. */
static void cp_decode_device_info_freqs(struct cp_device_info *info)
{
	info->minUHFFreq = (uint16_t)cp_bcd_to_uint32(info->minUHFFreq);
	info->maxUHFFreq = (uint16_t)cp_bcd_to_uint32(info->maxUHFFreq);
	info->minVHFFreq = (uint16_t)cp_bcd_to_uint32(info->minVHFFreq);
	info->maxVHFFreq = (uint16_t)cp_bcd_to_uint32(info->maxVHFFreq);
}

/* ---- CSS tone / lat-lon decode ------------------------------------------- */

struct cp_css codeplug_decode_css(uint16_t raw)
{
	struct cp_css out = { .type = CP_CSS_NONE, .value = 0, .inverted = false };

	if (raw == 0x0000 || raw == 0xFFFF) {
		return out;
	}
	if ((raw & 0xC000) == 0) {
		out.type = CP_CSS_CTCSS;
		out.value = (uint16_t)cp_bcd_to_uint32(raw);
		return out;
	}

	out.type = CP_CSS_DCS;
	out.inverted = (raw & 0x4000) != 0;

	uint16_t v = raw & 0x3FFF;
	uint16_t shift = 0;

	while (v) {
		out.value = (uint16_t)(out.value + ((v & 0xF) << shift));
		v >>= 4;
		shift += 3;
	}
	return out;
}

int32_t codeplug_decode_latlon(const uint8_t raw[3])
{
	uint32_t raw24 = ((uint32_t)raw[2] << 16) | ((uint32_t)raw[1] << 8) | raw[0];
	bool negative = (raw24 & 0x800000) != 0;
	int32_t degrees = (int32_t)((raw24 & 0x7FFFFF) >> 15);
	int32_t decimal = (int32_t)(raw24 & 0x7FFF);
	int32_t value = degrees * 10000 + decimal;

	return negative ? -value : value;
}

/* ---- Slot in-use checks --------------------------------------------------- */

/* name[0] == 0xFF marks an erased/never-written slot. Cast avoids relying on plain char's
 * implementation-defined signedness when comparing against 0xFF. */
static bool cp_slot_is_in_use(const char *name)
{
	return (uint8_t)name[0] != 0xFF;
}

bool codeplug_contact_is_in_use(const struct cp_contact *c)
{
	return cp_slot_is_in_use(c->name);
}

bool codeplug_channel_is_in_use(const struct cp_channel *ch)
{
	return cp_slot_is_in_use(ch->name);
}

bool codeplug_rx_group_is_in_use(const struct cp_rx_group *g)
{
	return cp_slot_is_in_use(g->name);
}

/* ---- Single-struct regions --------------------------------------------- */

int codeplug_get_device_info(struct cp_device_info *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, device_info)
	int ret = codeplug_raw_read(CP_OFFSET(device_info), out, sizeof(*out));

	if (ret == 0) {
		cp_decode_device_info_freqs(out);
	}
	return ret;
#else
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_general_settings(struct cp_general_settings *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, general_settings)
	return codeplug_raw_read(CP_OFFSET(general_settings), out, sizeof(*out));
#else
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_signalling_dtmf(struct cp_signalling_dtmf *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, signalling_dtmf)
	return codeplug_raw_read(CP_OFFSET(signalling_dtmf), out, sizeof(*out));
#else
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_signalling_dtmf_durations(struct cp_signalling_dtmf_durations *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, signalling_dtmf_durations)
	return codeplug_raw_read(CP_OFFSET(signalling_dtmf_durations), out, sizeof(*out));
#else
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_calibration(struct cp_calibration *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, calibration)
	return codeplug_raw_read(CP_OFFSET(calibration), out, sizeof(*out));
#else
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_nv_settings_raw(uint8_t *buf, size_t buf_len, uint32_t *magic_out)
{
#if DT_NODE_HAS_PROP(CP_MAP, nv_settings)
	size_t n = MIN(buf_len, (size_t)CP_SIZE(nv_settings));
	int ret = codeplug_raw_read(CP_OFFSET(nv_settings), buf, n);

	if (ret == 0 && magic_out) {
		if (n >= sizeof(*magic_out)) {
			memcpy(magic_out, buf, sizeof(*magic_out));
		} else {
			ret = codeplug_raw_read(CP_OFFSET(nv_settings), magic_out, sizeof(*magic_out));
		}
	}
	return ret;
#else
	ARG_UNUSED(buf);
	ARG_UNUSED(buf_len);
	ARG_UNUSED(magic_out);
	return -ENOTSUP;
#endif
}

int codeplug_set_nv_settings(const struct cp_nv_settings *in)
{
#if DT_NODE_HAS_PROP(CP_MAP, nv_settings)
	return codeplug_write(CP_OFFSET(nv_settings), in, sizeof(*in));
#else
	ARG_UNUSED(in);
	return -ENOTSUP;
#endif
}

/* ---- Indexed regions ---------------------------------------------------- */

#define CP_CHANNEL_SIZE        56
#define CP_CHANNELS_PER_BANK   128
#define CP_BANK_HEADER_SIZE    16
#define CP_BANK_SIZE           (CP_BANK_HEADER_SIZE + CP_CHANNELS_PER_BANK * CP_CHANNEL_SIZE)

int codeplug_get_channel(int index_1based, struct cp_channel *out)
{
	int zero_based, bank, in_bank;
	off_t base;

	if (index_1based < 1 || index_1based > 1024) {
		return -EINVAL;
	}
	zero_based = index_1based - 1;
	bank = zero_based / CP_CHANNELS_PER_BANK;
	in_bank = zero_based % CP_CHANNELS_PER_BANK;

	if (bank == 0) {
#if DT_NODE_HAS_PROP(CP_MAP, channels_bank1)
		base = CP_OFFSET(channels_bank1) + CP_BANK_HEADER_SIZE;
#else
		return -ENOTSUP;
#endif
	} else {
#if DT_NODE_HAS_PROP(CP_MAP, channels_ext)
		base = CP_OFFSET(channels_ext) + (off_t)(bank - 1) * CP_BANK_SIZE + CP_BANK_HEADER_SIZE;
#else
		return -ENOTSUP;
#endif
	}
	int ret = codeplug_raw_read(base + (off_t)in_bank * CP_CHANNEL_SIZE, out, sizeof(*out));

	if (ret == 0) {
		cp_decode_channel_freqs(out);
	}
	return ret;
}

int codeplug_get_contact(int index_1based, struct cp_contact *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, contacts)
	int ret;

	if (index_1based < 1 || index_1based > 1024) {
		return -EINVAL;
	}
	ret = codeplug_raw_read(CP_OFFSET(contacts) + (off_t)(index_1based - 1) * sizeof(*out),
				 out, sizeof(*out));
	if (ret == 0) {
		out->tgNumber = cp_bcd_to_uint32(cp_byteswap32(out->tgNumber));
	}
	return ret;
#else
	ARG_UNUSED(index_1based);
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_dtmf_contact(int index_1based, struct cp_dtmf_contact *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, dtmf_contacts)
	if (index_1based < 1 || index_1based > 63) {
		return -EINVAL;
	}
	return codeplug_raw_read(CP_OFFSET(dtmf_contacts) + (off_t)(index_1based - 1) * sizeof(*out),
				  out, sizeof(*out));
#else
	ARG_UNUSED(index_1based);
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_aprs_config(int index_1based, struct cp_aprs_config *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, aprs_configs)
	if (index_1based < 1 || index_1based > 8) {
		return -EINVAL;
	}
	return codeplug_raw_read(CP_OFFSET(aprs_configs) + (off_t)(index_1based - 1) * sizeof(*out),
				  out, sizeof(*out));
#else
	ARG_UNUSED(index_1based);
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_rx_group(int index_1based, struct cp_rx_group *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, rx_group_lists)
	off_t entries_base;

	if (index_1based < 1 || index_1based > 76) {
		return -EINVAL;
	}
	/* Entries start after a reserved 128-byte length/in-use array (only first 76 bytes meaningful). */
	entries_base = CP_OFFSET(rx_group_lists) + 128;
	return codeplug_raw_read(entries_base + (off_t)(index_1based - 1) * sizeof(*out),
				  out, sizeof(*out));
#else
	ARG_UNUSED(index_1based);
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_vfo_channel(int vfo_ab, struct cp_channel *out)
{
#if DT_NODE_HAS_PROP(CP_MAP, vfo_channels)
	int ret;

	if (vfo_ab != 0 && vfo_ab != 1) {
		return -EINVAL;
	}
	ret = codeplug_raw_read(CP_OFFSET(vfo_channels) + (off_t)vfo_ab * sizeof(*out),
				 out, sizeof(*out));
	if (ret == 0) {
		cp_decode_channel_freqs(out);
	}
	return ret;
#else
	ARG_UNUSED(vfo_ab);
	ARG_UNUSED(out);
	return -ENOTSUP;
#endif
}

int codeplug_get_quickkey(int index_0based, uint16_t *function_id_out)
{
#if DT_NODE_HAS_PROP(CP_MAP, quickkeys)
	if (index_0based < 0 || index_0based > 9) {
		return -EINVAL;
	}
	return codeplug_raw_read(CP_OFFSET(quickkeys) + (off_t)index_0based * sizeof(*function_id_out),
				  function_id_out, sizeof(*function_id_out));
#else
	ARG_UNUSED(index_0based);
	ARG_UNUSED(function_id_out);
	return -ENOTSUP;
#endif
}

/* ---- Zones: in-use bitmap + runtime format-detected list --------------- */

int codeplug_get_zone_inuse_bitmap(uint8_t bitmap[32])
{
#if DT_NODE_HAS_PROP(CP_MAP, zones_ext)
	return codeplug_raw_read(CP_OFFSET(zones_ext) + 0x10, bitmap, 32);
#else
	ARG_UNUSED(bitmap);
	return -ENOTSUP;
#endif
}

#if DT_NODE_HAS_PROP(CP_MAP, zones_ext)
/* At this offset, modern format has a channel-index high byte (0x00-0x04); legacy has zone-name text/0xFF fill. */
static bool cp_zone_is_modern_format(void)
{
	uint8_t probe;

	if (codeplug_raw_read(CP_OFFSET(zones_ext) + 0x6f, &probe, 1) < 0) {
		return true;
	}
	return probe <= 0x04;
}
#endif

int codeplug_get_zone(int slot_index_0based, struct cp_zone *out, int *channels_per_zone_out)
{
#if DT_NODE_HAS_PROP(CP_MAP, zones_ext)
	bool modern;
	int stride, channels_per_zone;
	off_t list_base, off;
	int ret;

	if (slot_index_0based < 0 || slot_index_0based >= 68) {
		return -EINVAL;
	}

	modern = cp_zone_is_modern_format();
	stride = modern ? 176 : 48;
	channels_per_zone = modern ? CP_ZONE_CHANNELS_MODERN : CP_ZONE_CHANNELS_LEGACY;
	list_base = CP_OFFSET(zones_ext) + 0x30;
	off = list_base + (off_t)slot_index_0based * stride;

	memset(out, 0, sizeof(*out));
	ret = codeplug_raw_read(off, out->name, sizeof(out->name));
	if (ret == 0) {
		ret = codeplug_raw_read(off + sizeof(out->name), out->channels,
					 (size_t)channels_per_zone * sizeof(out->channels[0]));
	}
	if (channels_per_zone_out) {
		*channels_per_zone_out = channels_per_zone;
	}
	return ret;
#else
	ARG_UNUSED(slot_index_0based);
	ARG_UNUSED(out);
	ARG_UNUSED(channels_per_zone_out);
	return -ENOTSUP;
#endif
}

/* ---- Region lookup (for the shell) -------------------------------------- */

int codeplug_list_regions(struct cp_region_info *out, int max)
{
	int n = 0;

#define CP_ADD_REGION(name_str, prop)                  \
	do {                                            \
		if (n < max) {                          \
			out[n].name = name_str;         \
			out[n].offset = CP_OFFSET(prop); \
			out[n].size = CP_SIZE(prop);    \
			n++;                             \
		}                                        \
	} while (0)

#if DT_NODE_HAS_PROP(CP_MAP, device_info)
	CP_ADD_REGION("device-info", device_info);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, general_settings)
	CP_ADD_REGION("general-settings", general_settings);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, signalling_dtmf)
	CP_ADD_REGION("signalling-dtmf", signalling_dtmf);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, signalling_dtmf_durations)
	CP_ADD_REGION("signalling-dtmf-durations", signalling_dtmf_durations);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, aprs_configs)
	CP_ADD_REGION("aprs-configs", aprs_configs);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, dtmf_contacts)
	CP_ADD_REGION("dtmf-contacts", dtmf_contacts);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, channels_bank1)
	CP_ADD_REGION("channels-bank1", channels_bank1);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, last_used_channel)
	CP_ADD_REGION("last-used-channel", last_used_channel);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, nv_settings)
	CP_ADD_REGION("nv-settings", nv_settings);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, boot_password)
	CP_ADD_REGION("boot-password", boot_password);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, quickkeys)
	CP_ADD_REGION("quickkeys", quickkeys);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, boot_lines)
	CP_ADD_REGION("boot-lines", boot_lines);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, vfo_channels)
	CP_ADD_REGION("vfo-channels", vfo_channels);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, zones_ext)
	CP_ADD_REGION("zones-ext", zones_ext);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, calibration)
	CP_ADD_REGION("calibration", calibration);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, custom_data)
	CP_ADD_REGION("custom-data", custom_data);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, channels_ext)
	CP_ADD_REGION("channels-ext", channels_ext);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, contacts)
	CP_ADD_REGION("contacts", contacts);
#endif
#if DT_NODE_HAS_PROP(CP_MAP, rx_group_lists)
	CP_ADD_REGION("rx-group-lists", rx_group_lists);
#endif

#undef CP_ADD_REGION
	return n;
}
