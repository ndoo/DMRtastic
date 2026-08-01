/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Read-only codeplug flash decoder -- byte-exact structs, see codeplug-map in the board DT.
 */

#ifndef DMRTASTIC_CODEPLUG_H_
#define DMRTASTIC_CODEPLUG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>        /* off_t */
#include <zephyr/toolchain.h> /* __packed */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Channel / contact / zone / RX group ------------------------------ */

/* Scalar fields (rxFreq/txFreq/tgNumber, device-info's freq range) are BCD-decoded in place by
 * their codeplug_get_*() accessor. CSS tone and lat/lon fields stay raw on the struct -- decode
 * them explicitly via codeplug_decode_css()/codeplug_decode_latlon().
 */

struct cp_channel {
	char name[16];
	uint32_t rxFreq; /* Hz */
	uint32_t txFreq; /* Hz */
	uint8_t chMode;
	uint8_t power;
	uint8_t locationLat0;
	uint8_t tot;
	uint8_t locationLat1;
	uint8_t locationLat2;
	uint8_t locationLon0;
	uint8_t locationLon1;
	uint16_t rxTone;
	uint16_t txTone;
	uint8_t locationLon2;
	uint8_t reserved1;
	uint8_t flag1;
	uint8_t rxSignaling;
	uint8_t artsInterval;
	uint8_t encrypt;
	uint8_t reserved2;
	uint8_t rxGroupList;
	uint8_t txColor;
	uint8_t aprsConfigIndex;
	uint16_t contact;
	uint8_t chFlag1;
	uint8_t chFlag2;
	uint8_t chFlag3;
	uint8_t chFlag4;
	uint16_t vfoOffsetFreq;
	uint8_t vfoFlag5;
	uint8_t sql;
}; /* 56 bytes on flash, no padding */

/* chFlag4 bit 0x02 -- channel bandwidth, set = 25 kHz, clear = 12.5 kHz (confirmed
 * against a reference channel-flag table for this codeplug format).
 */
#define CP_CHANNEL_FLAG4_BW_25K 0x02

struct cp_contact {
	char name[16];
	uint32_t tgNumber;
	uint8_t callType;
	uint8_t callRxTone;
	uint8_t ringStyle;
	uint8_t reserve1;
}; /* 24 bytes */

struct cp_dtmf_contact {
	char name[16];
	char code[16];
}; /* 32 bytes */

/* Format (modern 80ch/176B vs legacy 16ch/48B) is runtime-detected, not address-based; channels[]
 * holds the modern superset.
 */
#define CP_ZONE_CHANNELS_MODERN 80
#define CP_ZONE_CHANNELS_LEGACY 16

struct cp_zone {
	char name[16];
	uint16_t channels[CP_ZONE_CHANNELS_MODERN];
}; /* 176 bytes (modern format); only channels_per_zone entries valid */

struct cp_rx_group {
	char name[16];
	uint16_t contacts[32];
}; /* 80 bytes */

/* ---- Non-scalar field decoders (CSS tone, lat/lon fixed-point) --------- */

enum cp_css_type {
	CP_CSS_NONE = 0,
	CP_CSS_CTCSS,
	CP_CSS_DCS,
};

/* value: CTCSS -- tenths of Hz (2035 = 203.5 Hz). DCS -- octal-digit-packed code (print with
 * "%03o"). inverted: DCS-only, true for the inverted variant.
 */
struct cp_css {
	enum cp_css_type type;
	uint16_t value;
	bool inverted;
};

/* Decodes a raw cp_channel.rxTone/txTone value. */
struct cp_css codeplug_decode_css(uint16_t raw);

/* Decodes a raw 3-byte lat/lon fixed-point field (raw[0]=LSB..raw[2]=MSB, e.g. cp_channel's
 * locationLat0-2 or cp_aprs_config.latitude). Returns signed degrees x1e4 (-1234567 = -123.4567).
 */
int32_t codeplug_decode_latlon(const uint8_t raw[3]);

/* ---- General settings / device info ----------------------------------- */

struct cp_general_settings {
	char radioName[8];
	uint32_t radioId;
	uint8_t codeplugVersion;
	uint8_t reserve[3];
	uint8_t arsInitDly;
	uint8_t txPreambleDur;
	uint8_t monitorType;
	uint8_t voxSense;
	uint8_t rxLowBatt;
	uint8_t callAlertDur;
	uint8_t respTmr;
	uint8_t reminderTmr;
	uint8_t grpHang;
	uint8_t privateHang;
	uint8_t flag1;
	uint8_t flag2;
	uint8_t flag3;
	uint8_t flag4;
	uint8_t reserve2[2];
	char prgPwd[8];
}; /* 40 bytes */

struct cp_device_info {
	uint16_t minUHFFreq;
	uint16_t maxUHFFreq;
	uint16_t minVHFFreq;
	uint16_t maxVHFFreq;
	uint8_t lastPrgTime[6];
	uint16_t reserve2;
	char model[8];
	char sn[16];
	char cpsSwVer[8];
	char hardwareVer[8];
	char firmwareVer[8];
	char dspFwVer[24];
	uint8_t reserve3[8];
}; /* 96 bytes. Freq range fields are decoded to whole MHz by codeplug_get_device_info(). */

/* Durations sub-view (cp_signalling_dtmf_durations) is a separate on-flash region, not a slice of
 * this struct.
 */
struct cp_signalling_dtmf {
	uint8_t selfId[8];
	uint8_t killCode[16];
	uint8_t wakeCode[16];
	uint8_t delimiter;
	uint8_t groupCode;
	uint8_t decodeResp;
	uint8_t autoResetTimer;
	uint8_t flag1;
	uint8_t reserve1[3];
	uint8_t pttidUpCode[30];
	uint16_t reserve2;
	uint8_t pttidDownCode[30];
	uint16_t reserve3;
	uint8_t respHoldTime;
	uint8_t decTime;
	uint8_t fstDigitDly;
	uint8_t fstDur;
	uint8_t otherDur;
	uint8_t rate;
	uint8_t flag2;
	uint8_t reserve4;
}; /* 120 bytes */

/* Independently-addressed DTMF timing sub-block (own flash region). */
struct cp_signalling_dtmf_durations {
	uint8_t fstDigitDly;
	uint8_t fstDur;
	uint8_t otherDur;
	uint8_t rate;
	uint8_t tailDuration;
}; /* 5 bytes */

/* __packed: txFrequency would otherwise misalign at odd offset 55, growing the struct to 65 bytes.
 */
struct cp_aprs_config {
	char name[8];
	uint8_t senderSSID;
	uint8_t latitude[3];
	uint8_t longitude[3];
	struct {
		char name[6];
		uint8_t ssid;
	} paths[2];
	uint8_t iconTable;
	uint8_t iconIndex;
	char comment[24];
	uint32_t txFrequency;
	uint8_t reserved[2];
	uint8_t flags;
	uint16_t magicVer;
} __packed; /* 64 bytes */

/* ---- Calibration (local working copy, 512 bytes) ----------------------- */

struct cp_calibration {
	uint8_t voxLevel1;
	uint8_t voxLevel10;
	uint8_t rxLowVoltage;
	uint8_t rxHighVoltage;
	uint8_t rssi120;
	uint8_t rssi70;
	uint8_t unknown1[2];
	uint8_t unknown2;
	uint8_t uhfOscRefTune;
	uint8_t unknown3[2];
	uint8_t vhfOscRefTune;
	uint8_t unknown4[3];
	uint8_t uhfHighPowerCal[9];
	uint8_t vhfHighPowerCal[5];
	uint8_t unknown5[2];
	uint8_t uhfLowPowerCal[9];
	uint8_t vhfLowPowerCal[5];
	uint8_t unknown6[2];
	uint8_t uhfRxTuning[9];
	uint8_t vhfRxTuning[5];
	uint8_t unknown7[2];
	uint8_t uhfOpenSquelch9[9];
	uint8_t unknown8[7];
	uint8_t uhfCloseSquelch9[9];
	uint8_t unknown9[7];
	uint8_t uhfOpenSquelch1[9];
	uint8_t unknown10[7];
	uint8_t uhfCloseSquelch1[9];
	uint8_t unknown11[7];
	uint8_t unknown12[16];
	uint8_t uhfCtc67[9];
	uint8_t unknown13[2];
	uint8_t vhfCtc67[5];
	uint8_t uhfCtc151[9];
	uint8_t unknown14[2];
	uint8_t vhfCtc151[5];
	uint8_t uhfCtc254[9];
	uint8_t unknown15[2];
	uint8_t vhfCtc254[5];
	uint8_t unknown16[16];
	uint8_t uhfDcs[9];
	uint8_t unknown17[2];
	uint8_t vhfDcs[5];
	uint8_t vhfOpenSquelch9[5];
	uint8_t vhfCloseSquelch9[5];
	uint8_t vhfOpenSquelch1[5];
	uint8_t vhfCloseSquelch1[5];
	uint8_t unknown18[12];
	uint8_t vhfCalFreqs[10][4];
	uint8_t unknown19[8];
	uint8_t uhfDmrIGain[9];
	uint8_t vhfDmrIGain[5];
	uint8_t unknown20[2];
	uint8_t uhfDmrQGain[9];
	uint8_t vhfDmrQGain[5];
	uint8_t unknown21[2];
	uint8_t unknown22[32];
	uint8_t uhfFmIGain[9];
	uint8_t vhfFmIGain[5];
	uint8_t unknown23[2];
	uint8_t uhfFmQGain[9];
	uint8_t vhfFmQGain[5];
	uint8_t unknown24[2];
	uint8_t uhfMidPowerCal[9];
	uint8_t vhfMidPowerCal[5];
	uint8_t unknown25[2];
	uint8_t uhfVeryLowPowerCal[9];
	uint8_t vhfVeryLowPowerCal[5];
	uint8_t unknown26[2];
	uint8_t uhfCalFreqs[18][4];
	uint8_t unknown27[8];
}; /* 512 bytes, no padding (all-uint8_t fields) */

/* ---- On-flash settings block ------------------------------------------- */

/* Best-effort decode, valid only when magic == CP_NV_SETTINGS_MAGIC_LATEST -- shape isn't modeled
 * for older magics.
 */
#define CP_NV_SETTINGS_MAGIC_LATEST 0x4780U

struct cp_nv_settings {
	uint32_t magicNumber;
	uint32_t lat;
	uint32_t lon;
	uint8_t timezone;
	uint8_t beepOptions;
	uint16_t vfoSweepSettings;
	uint32_t overrideTG;
	uint32_t vfoScanLow[2];
	uint32_t vfoScanHigh[2];
	uint32_t bitfieldOptions;
	uint32_t aprsBeaconingSettingsPart1[2];
	int16_t currentIndexInTRxGroupList[3];
	int16_t currentZone;
	uint16_t userPower;
	uint16_t tsManualOverride;
	int16_t currentChannelIndexInZone;
	int16_t currentChannelIndexInAllZone;
	uint16_t aprsBeaconingSettingsPart2;
	uint8_t txPowerLevel;
	uint8_t txTimeoutBeepX5Secs;
	uint8_t beepVolumeDivider;
	uint8_t micGainDMR;
	uint8_t micGainFM;
	uint8_t backlightMode;
	uint8_t backLightTimeout;
	int8_t displayContrast;
	int8_t displayBacklightPercentage[2];
	int8_t displayBacklightPercentageOff;
	uint8_t initialMenuNumber;
	uint8_t extendedInfosOnScreen;
	uint8_t txFreqLimited;
	uint8_t scanModePause;
	uint8_t scanDelay;
	uint8_t dmrRxAGC;
	uint8_t hotspotType;
	uint8_t scanStepTime;
	uint8_t currentVFONumber;
	uint8_t dmrDestinationFilter;
	uint8_t dmrCaptureTimeout;
	uint8_t dmrCcTsFilter;
	uint8_t analogFilterLevel;
	uint8_t privateCalls;
	uint8_t contactDisplayPriority;
	uint8_t splitContact;
	uint8_t voxThreshold;
	uint8_t voxTailUnits;
	uint8_t audioPromptMode;
	int8_t temperatureCalibration;
	uint8_t batteryCalibration;
	uint8_t squelchDefaults[8];
	uint8_t ecoLevel;
	uint8_t apo;
	uint8_t keypadTimerLong;
	uint8_t keypadTimerRepeat;
	uint8_t autolockTimer;
	uint32_t roaming;
	uint8_t lastTalkerOnScreenTimer;
};

/* ---- Generic read primitive -------------------------------------------- */

int codeplug_raw_read(off_t offset, void *buf, size_t len);

/* Extension point for a future write path (CPS communication). Not implemented --
 * always returns -ENOTSUP, logging the call so callers can be confirmed end-to-end.
 */
int codeplug_write(off_t offset, const void *buf, size_t len);

/* ---- Per-region accessors ---------------------------------------------- */

int codeplug_get_device_info(struct cp_device_info *out);
int codeplug_get_general_settings(struct cp_general_settings *out);
int codeplug_get_channel(int index_1based, struct cp_channel *out);
/* Hardware-confirmed: index 1024 on a real codeplug reads name[0]==0xFF and is correctly reported
 * unused.
 */
bool codeplug_channel_is_in_use(const struct cp_channel *ch);
int codeplug_get_contact(int index_1based, struct cp_contact *out);
bool codeplug_contact_is_in_use(const struct cp_contact *c);
int codeplug_get_dtmf_contact(int index_1based, struct cp_dtmf_contact *out);
/* slot_index_0based is a physical slot (0..67), not "the nth populated zone" -- see
 * codeplug_get_zone_inuse_bitmap().
 */
int codeplug_get_zone(int slot_index_0based, struct cp_zone *out, int *channels_per_zone_out);
int codeplug_get_zone_inuse_bitmap(uint8_t bitmap[32]);
int codeplug_get_rx_group(int index_1based, struct cp_rx_group *out); /* 1..76 */
/* Unlike codeplug_channel_is_in_use(), not directly hardware-exercised -- same convention, untested
 * slot type.
 */
bool codeplug_rx_group_is_in_use(const struct cp_rx_group *g);
int codeplug_get_aprs_config(int index_1based, struct cp_aprs_config *out); /* 1..8 */
int codeplug_get_signalling_dtmf(struct cp_signalling_dtmf *out);
int codeplug_get_signalling_dtmf_durations(struct cp_signalling_dtmf_durations *out);
int codeplug_get_vfo_channel(int vfo_ab, struct cp_channel *out);       /* 0=A, 1=B */
int codeplug_get_quickkey(int index_0based, uint16_t *function_id_out); /* 0..9 */
int codeplug_get_nv_settings_raw(uint8_t *buf, size_t buf_len, uint32_t *magic_out);
/* Serializes in as-is over the on-flash nv-settings block. Caller should have obtained
 * in via codeplug_get_nv_settings_raw() (magic-gated) and patched only the fields it
 * owns, to avoid clobbering unrelated settings. No-op until codeplug_write() is
 * implemented.
 */
int codeplug_set_nv_settings(const struct cp_nv_settings *in);
int codeplug_get_calibration(struct cp_calibration *out);
int codeplug_get_jedec_id(uint8_t id[3]);

/* Region lookup for the shell's "cp list"/"cp dump" commands. */
struct cp_region_info {
	const char *name;
	uint32_t offset;
	uint32_t size;
};

/* Returns the number of known regions and fills entries via out[i], up to max. */
int codeplug_list_regions(struct cp_region_info *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CODEPLUG_H_ */
