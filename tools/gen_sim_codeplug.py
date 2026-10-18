#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
"""
Convert a CPS JSON export (compatible with OpenGD77 codeplug/CPS) into the binary
codeplug flash image that src/model/codeplug.c reads, for use by the native_sim.

Region offsets follow the codeplug-map node in the board devicetree; struct layouts
follow src/model/codeplug.h. Output is deterministic.
"""

import argparse
import json
import re
import struct
import sys
from pathlib import Path

DEFAULT_IN = Path(__file__).resolve().parent.parent / "local" / "radio_codeplug.json"
DEFAULT_SIZE = 999424

ADDR_DEVICE_INFO = 0x80
ADDR_GENERAL = 0xE0
ADDR_APRS = 0x1588
ADDR_CH_BANK1 = 0x3780
ADDR_NV = 0x604B
ADDR_BOOT_INTRO = 0x7518
ADDR_BOOT_LINES = 0x7540
ADDR_VFO = 0x7590
ADDR_ZONES = 0x8000
ADDR_CH_EXT = 0x9B1B0
ADDR_CONTACTS = 0xA7620
ADDR_RXGROUP = 0xAD620

CH_SIZE = 56
CH_PER_BANK = 128
BANK_HDR = 16
CH_MAX = 1024
CONTACT_SIZE = 24
CONTACT_MAX = 1024
RXGROUP_MAX = 76
RXGROUP_CONTACTS = 32
ZONE_MAX = 68
ZONE_CHANNELS = 80
ZONE_STRIDE = 16 + 2 * ZONE_CHANNELS
APRS_MAX = 8
NV_MAGIC = 0x4780
NV_SIZE = 0x100

F4_BW25 = 0x02
F4_RXONLY = 0x04
F4_ALLSKIP = 0x10
F4_ZONESKIP = 0x20
F4_VOX = 0x40

# W per level; per-channel power byte is index + 1, 0 = radio default
POWER_W = (0.05, 0.25, 0.5, 0.75, 1.0, 2.0, 3.0, 5.0, 10.0)
VFO_STEP_KHZ = (2.5, 5.0, 6.25, 10.0, 12.5, 25.0, 30.0, 50.0)


class CodeplugError(Exception):
    pass


def warn(msg):
    print(f"warning: {msg}", file=sys.stderr)


def int2bcd(n):
    out = 0
    shift = 0
    while n:
        out |= (n % 10) << shift
        n //= 10
        shift += 4
    return out


def text_field(s, size):
    raw = "".join(c if 32 <= ord(c) < 127 else "?" for c in (s or ""))
    return raw.encode("ascii")[:size].ljust(size, b"\xff")


def freq_units(mhz, what):
    if mhz is None:
        return 0
    try:
        v = round(float(mhz) * 1e5)
    except (TypeError, ValueError):
        raise CodeplugError(f"{what}: bad frequency {mhz!r}")
    if v < 0 or v > 99999999:
        raise CodeplugError(f"{what}: frequency {mhz!r} out of range")
    return v


def parse_tone(val, what):
    if val is None:
        return 0xFFFF
    s = str(val).strip()
    if s == "" or s.lower() in ("none", "off"):
        return 0xFFFF
    m = re.fullmatch(r"D?(\d{3})([NI]?)", s, re.I) if s[:1] in "Dd" else None
    if m:
        code = int(m.group(1), 16)
        raw = 0x8000 | code
        if m.group(2).upper() == "I":
            raw |= 0x4000
        return raw
    try:
        tenths = round(float(s.rstrip("Hz hz")) * 10)
    except ValueError:
        warn(f"{what}: unrecognised tone {val!r}, treated as none")
        return 0xFFFF
    if tenths <= 0:
        return 0xFFFF
    if tenths > 3999:
        warn(f"{what}: CTCSS {val!r} out of range, treated as none")
        return 0xFFFF
    return int2bcd(tenths)


def parse_power(val):
    if val is None:
        return 0
    s = str(val)
    m = re.match(r"\s*([\d.]+)\s*(mW|W)\b", s, re.I)
    if not m:
        return 0
    w = float(m.group(1)) / (1000.0 if m.group(2).lower() == "mw" else 1.0)
    best = min(range(len(POWER_W)), key=lambda i: abs(POWER_W[i] - w))
    return best + 1


def parse_squelch(val):
    if val is None:
        return 0
    s = str(val).strip()
    m = re.fullmatch(r"(\d+)\s*%", s)
    if m:
        return min(21, int(m.group(1)) // 5 + 1)
    if s.isdigit():
        return min(21, int(s))
    return 0


def parse_tot(val):
    if val is None:
        return 0
    m = re.match(r"\s*(\d+)", str(val))
    if not m:
        return 0
    return min(255, int(m.group(1)) // 15)


def parse_ts_override(val):
    s = "" if val is None else str(val)
    if "1" in s:
        return 0x00
    if "2" in s:
        return 0x02
    return 0x01


def pack_latlon(deg):
    neg = deg < 0
    a = abs(deg)
    whole = int(a)
    frac = min(0x7FFF, round((a - whole) * 32768))
    raw = (whole << 15) | frac
    if neg:
        raw |= 0x800000
    return raw & 0xFFFFFF


def pack_channel(ch, contacts_idx, tglists_idx, aprs_idx, what, is_vfo=False):
    digital = str(ch.get("type", "Analogue")).lower().startswith("d")
    f1 = 0
    rx_sig = 0
    arts = 0x16
    enc = 0
    if ch.get("overrideDmrId") and ch.get("dmrId"):
        did = int(ch["dmrId"])
        if not 1 <= did <= 0xFFFFFF:
            raise CodeplugError(f"{what}: DMR ID override {did} out of range")
        f1 |= 0x80
        rx_sig, arts, enc = (did >> 16) & 0xFF, (did >> 8) & 0xFF, did & 0xFF
    if ch.get("noBeep"):
        f1 |= 0x40
    if ch.get("noEco"):
        f1 |= 0x20
    if ch.get("useLocation"):
        f1 |= 0x08
    if ch.get("forceDmo"):
        f1 |= 0x04

    f4 = F4_BW25 if float(ch.get("bandwidth", 12.5)) >= 25 else 0
    if ch.get("rxOnly"):
        f4 |= F4_RXONLY
    if ch.get("allSkip"):
        f4 |= F4_ALLSKIP
    if ch.get("zoneSkip"):
        f4 |= F4_ZONESKIP
    if ch.get("vox"):
        f4 |= F4_VOX

    f2 = 0x40 if int(ch.get("timeslot", 1) or 1) == 2 else 0

    contact = 0
    if ch.get("contact") is not None:
        contact = contacts_idx.get(ch["contact"], 0)
        if not contact:
            warn(f"{what}: unknown contact {ch['contact']!r}")
    tgl = 0
    if ch.get("tgList") is not None:
        tgl = tglists_idx.get(ch["tgList"], 0)
        if not tgl:
            warn(f"{what}: unknown TG list {ch['tgList']!r}")
    aprs = aprs_idx.get(ch.get("aprs"), 0) if ch.get("aprs") not in (None, "None") else 0

    lat = pack_latlon(float(ch.get("latitude") or 0))
    lon = pack_latlon(float(ch.get("longitude") or 0))
    step = 1
    if is_vfo and ch.get("step") is not None:
        try:
            step = min(range(8), key=lambda i: abs(VFO_STEP_KHZ[i] - float(ch["step"])))
        except (TypeError, ValueError):
            pass

    name = ch.get("name") or ("VFO" if is_vfo else "")
    return struct.pack(
        "<16sIIBBBBBBBBHHBBBBBBBBBBHBBBBHBB",
        text_field(name, 16),
        int2bcd(freq_units(ch.get("rxFreq"), what + " rxFreq")),
        int2bcd(freq_units(ch.get("txFreq"), what + " txFreq")),
        1 if digital else 0,
        parse_power(ch.get("power")),
        lat & 0xFF,
        parse_tot(ch.get("tot")),
        (lat >> 8) & 0xFF,
        (lat >> 16) & 0xFF,
        lon & 0xFF,
        (lon >> 8) & 0xFF,
        parse_tone(ch.get("rxTone"), what + " rxTone"),
        parse_tone(ch.get("txTone"), what + " txTone"),
        (lon >> 16) & 0xFF,
        0,
        f1,
        rx_sig,
        arts,
        enc,
        0,
        tgl,
        max(0, min(15, int(ch.get("colorCode", 1) or 0))) if digital or is_vfo else 1,
        aprs,
        contact,
        0,
        f2,
        0,
        f4,
        0,
        step << 4,
        parse_squelch(ch.get("squelch")),
    )


NV_FIELDS = (
    ("magicNumber", "I", 1), ("lat", "I", 1), ("lon", "I", 1),
    ("timezone", "B", 1), ("beepOptions", "B", 1), ("vfoSweepSettings", "H", 1),
    ("overrideTG", "I", 1), ("vfoScanLow", "I", 2), ("vfoScanHigh", "I", 2),
    ("bitfieldOptions", "I", 1), ("aprsBeaconingSettingsPart1", "I", 2),
    ("currentIndexInTRxGroupList", "h", 3), ("currentZone", "h", 1), ("userPower", "H", 1),
    ("tsManualOverride", "H", 1), ("currentChannelIndexInZone", "h", 1),
    ("currentChannelIndexInAllZone", "h", 1), ("aprsBeaconingSettingsPart2", "H", 1),
    ("txPowerLevel", "B", 1), ("txTimeoutBeepX5Secs", "B", 1), ("beepVolumeDivider", "B", 1),
    ("micGainDMR", "B", 1), ("micGainFM", "B", 1), ("backlightMode", "B", 1),
    ("backLightTimeout", "B", 1), ("displayContrast", "b", 1),
    ("displayBacklightPercentage", "b", 2), ("displayBacklightPercentageOff", "b", 1),
    ("initialMenuNumber", "B", 1), ("extendedInfosOnScreen", "B", 1),
    ("txFreqLimited", "B", 1), ("scanModePause", "B", 1), ("scanDelay", "B", 1),
    ("dmrRxAGC", "B", 1), ("hotspotType", "B", 1), ("scanStepTime", "B", 1),
    ("currentVFONumber", "B", 1), ("dmrDestinationFilter", "B", 1),
    ("dmrCaptureTimeout", "B", 1), ("dmrCcTsFilter", "B", 1), ("analogFilterLevel", "B", 1),
    ("privateCalls", "B", 1), ("contactDisplayPriority", "B", 1), ("splitContact", "B", 1),
    ("voxThreshold", "B", 1), ("voxTailUnits", "B", 1), ("audioPromptMode", "B", 1),
    ("temperatureCalibration", "b", 1), ("batteryCalibration", "B", 1),
    ("squelchDefaults", "B", 8), ("ecoLevel", "B", 1), ("apo", "B", 1),
    ("keypadTimerLong", "B", 1), ("keypadTimerRepeat", "B", 1), ("autolockTimer", "B", 1),
    ("roaming", "I", 1), ("lastTalkerOnScreenTimer", "B", 1),
)

NV_DEFAULTS = {
    "magicNumber": NV_MAGIC,
    "txPowerLevel": 4,
    "beepVolumeDivider": 1,
    "displayBacklightPercentage": 100,
    "displayBacklightPercentageOff": 10,
    "squelchDefaults": 10,
}


def nv_block():
    buf = bytearray()
    for name, code, count in NV_FIELDS:
        size = struct.calcsize(code)
        for _ in range(count):
            buf += b"\0" * (-len(buf) % size)
            buf += struct.pack("<" + code, NV_DEFAULTS.get(name, 0))
    if len(buf) > NV_SIZE:
        raise CodeplugError("internal: nv-settings block too large")
    return bytes(buf)


def put(img, addr, data):
    if addr + len(data) > len(img):
        raise CodeplugError(f"image too small: need 0x{addr + len(data):x}, have 0x{len(img):x}")
    img[addr:addr + len(data)] = data


def first_index(items, key="name"):
    out = {}
    for i, it in enumerate(items):
        n = it.get(key)
        if n is not None and n not in out:
            out[n] = i + 1
    return out


def build(cfg, size=DEFAULT_SIZE):
    img = bytearray(b"\xff" * size)
    gen = cfg.get("general") or {}

    contacts = cfg.get("contacts") or []
    if len(contacts) > CONTACT_MAX:
        raise CodeplugError(f"{len(contacts)} contacts exceeds {CONTACT_MAX}")
    tglists = cfg.get("tgLists") or []
    if len(tglists) > RXGROUP_MAX:
        raise CodeplugError(f"{len(tglists)} TG lists exceeds {RXGROUP_MAX}")
    contacts_idx = first_index(contacts)
    tglists_idx = first_index(tglists)
    aprs_idx = first_index(cfg.get("aprs") or [])

    # device info: band limits as BCD whole MHz
    lim = cfg.get("bandLimits") or {}
    di = bytearray(b"\x00" * 96)
    for off, key, dflt in ((0, "uhfMin", 400), (2, "uhfMax", 480), (4, "vhfMin", 136),
                           (6, "vhfMax", 174)):
        mhz = round(float(lim.get(key, dflt * 1_000_000)) / 1e6)
        struct.pack_into("<H", di, off, int2bcd(mhz))
    di[16:24] = b"MD-UV390"
    put(img, ADDR_DEVICE_INFO, bytes(di))

    # general settings
    gs = bytearray(b"\xff" * 40)
    gs[0:8] = text_field(gen.get("radioName") or gen.get("callsign"), 8)
    did = int(gen.get("dmrId") or 0)
    if not 0 <= did <= 99999999:
        raise CodeplugError(f"general.dmrId {did} out of range")
    gs[8:12] = int2bcd(did).to_bytes(4, "big")
    put(img, ADDR_GENERAL, bytes(gs))

    # boot screen
    mode = str(gen.get("bootScreenMode", "")).lower()
    img[ADDR_BOOT_INTRO] = 0 if "pic" in mode or "logo" in mode else 1
    img[ADDR_BOOT_INTRO + 1] = 1 if gen.get("bootPasswordEnabled") else 0
    put(img, ADDR_BOOT_LINES, text_field(gen.get("infoLine1"), 16))
    put(img, ADDR_BOOT_LINES + 16, text_field(gen.get("infoLine2"), 16))

    # channels
    chans = sorted(cfg.get("channels") or [], key=lambda c: int(c["number"]))
    seen = set()
    bitmaps = {}
    by_name = {}
    for ch in chans:
        n = int(ch["number"])
        if not 1 <= n <= CH_MAX:
            raise CodeplugError(f"channel number {n} out of range 1..{CH_MAX}")
        if n in seen:
            raise CodeplugError(f"duplicate channel number {n}")
        seen.add(n)
        z = n - 1
        bank, slot = divmod(z, CH_PER_BANK)
        base = ADDR_CH_BANK1 if bank == 0 else ADDR_CH_EXT + (bank - 1) * (
            BANK_HDR + CH_PER_BANK * CH_SIZE)
        put(img, base + BANK_HDR + slot * CH_SIZE,
            pack_channel(ch, contacts_idx, tglists_idx, aprs_idx, f"channel {n}"))
        bm = bitmaps.setdefault(base, bytearray(16))
        bm[slot // 8] |= 1 << (slot % 8)
        by_name.setdefault(ch.get("name"), n)
    for base in (ADDR_CH_BANK1, *(ADDR_CH_EXT + b * (BANK_HDR + CH_PER_BANK * CH_SIZE)
                                  for b in range(7))):
        put(img, base, bytes(bitmaps.get(base, bytearray(16))))

    # VFOs
    for i, key in enumerate(("vfoA", "vfoB")):
        v = cfg.get(key)
        if v is None:
            continue
        v = dict(v)
        v.setdefault("name", f"VFO {'AB'[i]}")
        put(img, ADDR_VFO + i * CH_SIZE,
            pack_channel(v, contacts_idx, tglists_idx, aprs_idx, key, is_vfo=True))

    # zones
    zones = cfg.get("zones") or []
    if len(zones) > ZONE_MAX:
        warn(f"{len(zones)} zones, keeping first {ZONE_MAX}")
        zones = zones[:ZONE_MAX]
    zbitmap = bytearray(32)
    for i, z in enumerate(zones):
        idx = []
        for name in z.get("channels") or []:
            n = by_name.get(name)
            if n is None:
                warn(f"zone {z.get('name')!r}: unknown channel {name!r}")
                continue
            idx.append(n)
        if len(idx) > ZONE_CHANNELS:
            warn(f"zone {z.get('name')!r}: {len(idx)} channels, keeping first {ZONE_CHANNELS}")
            idx = idx[:ZONE_CHANNELS]
        rec = text_field(z.get("name"), 16) + struct.pack(
            f"<{ZONE_CHANNELS}H", *idx, *([0] * (ZONE_CHANNELS - len(idx))))
        put(img, ADDR_ZONES + 0x30 + i * ZONE_STRIDE, rec)
        zbitmap[i // 8] |= 1 << (i % 8)
    put(img, ADDR_ZONES + 0x10, bytes(zbitmap))

    # contacts
    kinds = {"group": 0, "private": 1, "all": 2}
    for i, c in enumerate(contacts):
        ctype = kinds.get(str(c.get("type", "Group")).lower(), 0)
        cid = int(c.get("dmrId") or 0)
        if not 0 <= cid <= 99999999:
            raise CodeplugError(f"contact {c.get('name')!r}: ID {cid} out of range")
        put(img, ADDR_CONTACTS + i * CONTACT_SIZE,
            text_field(c.get("name"), 16) + int2bcd(cid).to_bytes(4, "big") +
            bytes([ctype, 0, 0, parse_ts_override(c.get("tsOverride"))]))

    # RX group lists
    for i, g in enumerate(tglists):
        members = []
        for name in g.get("contacts") or []:
            ci = contacts_idx.get(name)
            if ci is None:
                warn(f"TG list {g.get('name')!r}: unknown contact {name!r}")
                continue
            members.append(ci)
        if len(members) > RXGROUP_CONTACTS:
            warn(f"TG list {g.get('name')!r}: {len(members)} contacts, keeping first "
                 f"{RXGROUP_CONTACTS}")
            members = members[:RXGROUP_CONTACTS]
        img[ADDR_RXGROUP + i] = len(members) + 1
        put(img, ADDR_RXGROUP + 128 + i * 80,
            text_field(g.get("name"), 16) + struct.pack(
                "<32H", *members, *([0] * (RXGROUP_CONTACTS - len(members)))))
    for i in range(len(tglists), RXGROUP_MAX):
        img[ADDR_RXGROUP + i] = 0

    # APRS configs (name and comment only)
    for i, a in enumerate((cfg.get("aprs") or [])[:APRS_MAX]):
        rec = bytearray(64)
        rec[0:8] = text_field(a.get("name"), 8)
        rec[8] = int(a.get("ssid") or 0) & 0xFF
        rec[31:55] = text_field(a.get("comment"), 24)
        struct.pack_into("<H", rec, 62, 0x4152)
        put(img, ADDR_APRS + i * 64, bytes(rec))

    put(img, ADDR_NV, nv_block())
    return bytes(img)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--in", dest="src", type=Path, default=DEFAULT_IN,
                    help=f"CPS JSON export (default: {DEFAULT_IN})")
    ap.add_argument("--out", type=Path, required=True, help="output flash image")
    ap.add_argument("--size", type=int, default=DEFAULT_SIZE, help="image size in bytes")
    args = ap.parse_args(argv)
    try:
        try:
            cfg = json.loads(args.src.read_text())
        except OSError as e:
            raise CodeplugError(f"cannot read {args.src}: {e.strerror}")
        except json.JSONDecodeError as e:
            raise CodeplugError(f"{args.src}: invalid JSON: {e}")
        if not isinstance(cfg, dict):
            raise CodeplugError(f"{args.src}: top level is not an object")
        radio = (cfg.get("general") or {}).get("radioType")
        if radio not in (None, "MK22"):
            warn(f"radioType {radio!r}, expected 'MK22'")
        img = build(cfg, args.size)
    except (CodeplugError, KeyError, ValueError, TypeError) as e:
        print(f"error: {type(e).__name__}: {e}" if not isinstance(e, CodeplugError)
              else f"error: {e}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(img)
    return 0


if __name__ == "__main__":
    sys.exit(main())
