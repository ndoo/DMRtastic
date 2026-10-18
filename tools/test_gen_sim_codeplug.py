#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
"""Round-trip check for gen_sim_codeplug.py using an independent reader of the flash layout."""

import ctypes
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
GEN = HERE / "gen_sim_codeplug.py"
FIXTURE = HERE / "testdata" / "sim_codeplug_min.json"


class NvSettings(ctypes.Structure):
    _fields_ = [
        ("magicNumber", ctypes.c_uint32), ("lat", ctypes.c_uint32), ("lon", ctypes.c_uint32),
        ("timezone", ctypes.c_uint8), ("beepOptions", ctypes.c_uint8),
        ("vfoSweepSettings", ctypes.c_uint16), ("overrideTG", ctypes.c_uint32),
        ("vfoScanLow", ctypes.c_uint32 * 2), ("vfoScanHigh", ctypes.c_uint32 * 2),
        ("bitfieldOptions", ctypes.c_uint32), ("aprs1", ctypes.c_uint32 * 2),
        ("trxIdx", ctypes.c_int16 * 3), ("currentZone", ctypes.c_int16),
        ("userPower", ctypes.c_uint16), ("tsManualOverride", ctypes.c_uint16),
        ("chInZone", ctypes.c_int16), ("chInAll", ctypes.c_int16), ("aprs2", ctypes.c_uint16),
        ("b", ctypes.c_uint8 * 7), ("displayContrast", ctypes.c_int8),
        ("backlight", ctypes.c_int8 * 2), ("backlightOff", ctypes.c_int8),
        ("c", ctypes.c_uint8 * 19), ("temp", ctypes.c_int8), ("batt", ctypes.c_uint8),
        ("squelchDefaults", ctypes.c_uint8 * 8), ("d", ctypes.c_uint8 * 5),
        ("roaming", ctypes.c_uint32), ("lastTalker", ctypes.c_uint8),
    ]


def bcd(n):
    return int(format(n, "x"))


def text(b):
    return b.rstrip(b"\xff\x00").decode("ascii")


def channel_at(img, off):
    f = struct.unpack_from("<16sIIBBBBBBBBHHBBBBBBBBBBHBBBBHBB", img, off)
    keys = ("name rx tx mode power lat0 tot lat1 lat2 lon0 lon1 rxTone txTone lon2 r1 f1 "
            "rxSig arts enc r2 tgl cc aprs contact c1 f2 f3 f4 vfoOff vfoF5 sql").split()
    d = dict(zip(keys, f))
    d["name"] = text(d["name"])
    d["rx_hz"] = bcd(d["rx"]) * 10
    d["tx_hz"] = bcd(d["tx"]) * 10
    return d


def ch_off(n):
    bank, slot = divmod(n - 1, 128)
    base = 0x3780 if bank == 0 else 0x9B1B0 + (bank - 1) * (16 + 128 * 56)
    return base, base + 16 + slot * 56


def run_gen(src, out):
    return subprocess.run([sys.executable, str(GEN), "--in", str(src), "--out", str(out)],
                          capture_output=True, text=True)


class RoundTrip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        out = cls.dir / "cp.bin"
        r = run_gen(FIXTURE, out)
        assert r.returncode == 0, r.stderr
        cls.img = out.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_size_and_determinism(self):
        self.assertEqual(len(self.img), 999424)
        out2 = self.dir / "cp2.bin"
        self.assertEqual(run_gen(FIXTURE, out2).returncode, 0)
        self.assertEqual(out2.read_bytes(), self.img)

    def test_general_and_device_info(self):
        img = self.img
        self.assertEqual(text(img[0xE0:0xE8]), "N0CALL")
        self.assertEqual(bcd(int.from_bytes(img[0xE8:0xEC], "big")), 1234567)
        umin, umax, vmin, vmax = struct.unpack_from("<4H", img, 0x80)
        self.assertEqual((bcd(vmin), bcd(vmax), bcd(umin), bcd(umax)), (136, 174, 400, 480))
        self.assertEqual(text(img[0x7540:0x7550]), "Test Radio")
        self.assertEqual(text(img[0x7550:0x7560]), "Line Two")

    def test_channels(self):
        img = self.img
        hdr, _ = ch_off(1)
        self.assertEqual(img[hdr], 0b00011111)
        self.assertEqual(img[hdr + 1], 0b00000001)
        c1 = channel_at(img, ch_off(1)[1])
        self.assertEqual((c1["name"], c1["rx_hz"], c1["tx_hz"], c1["mode"]),
                         ("Simplex 2m", 146520000, 146520000, 0))
        self.assertTrue(c1["f4"] & 0x02)
        self.assertEqual((c1["rxTone"], c1["txTone"], c1["power"], c1["sql"]),
                         (0xFFFF, 0xFFFF, 0, 0))
        c2 = channel_at(img, ch_off(2)[1])
        self.assertEqual((c2["rx_hz"], c2["tx_hz"], c2["rxTone"], c2["power"]),
                         (147000000, 147600000, 0x1035, 3))
        self.assertFalse(c2["f4"] & 0x02)
        c3 = channel_at(img, ch_off(3)[1])
        self.assertEqual((c3["rxTone"], c3["txTone"], c3["power"]), (0x8023, 0xC754, 8))
        self.assertTrue(c3["f4"] & 0x20)
        self.assertTrue(channel_at(img, ch_off(4)[1])["f4"] & 0x04)
        c5 = channel_at(img, ch_off(5)[1])
        self.assertEqual((c5["mode"], c5["cc"], c5["f2"] & 0x40, c5["contact"], c5["tgl"]),
                         (1, 1, 0x40, 1, 1))
        self.assertEqual(c5["f1"] & 0x80, 0x80)
        self.assertEqual((c5["rxSig"] << 16) | (c5["arts"] << 8) | c5["enc"], 7654321)
        self.assertEqual(c5["tot"], 12)
        self.assertEqual(channel_at(img, ch_off(9)[1])["name"], "Simplex 2m")
        self.assertEqual(img[ch_off(6)[1]], 0xFF)
        self.assertEqual(img[ch_off(129)[1]], 0xFF)
        self.assertEqual(img[0x9B1B0], 0)

    def test_vfo(self):
        a = channel_at(self.img, 0x7590)
        b = channel_at(self.img, 0x7590 + 56)
        self.assertEqual((a["rx_hz"], a["mode"]), (145500000, 0))
        self.assertEqual((b["rx_hz"], b["f4"] & 2), (433000000, 2))

    def test_zones(self):
        img = self.img
        self.assertEqual(img[0x8010], 0b11)
        self.assertEqual(img[0x8011:0x8030], b"\x00" * 31)
        for slot, (name, chans) in enumerate((("Analogue", [1, 2, 3]), ("Digital", [5, 4]))):
            off = 0x8030 + slot * 176
            self.assertEqual(text(img[off:off + 16]), name)
            vals = struct.unpack_from("<80H", img, off + 16)
            self.assertEqual(list(vals[:len(chans)]), chans)
            self.assertTrue(all(v == 0 for v in vals[len(chans):]))
        self.assertLessEqual(img[0x806F], 4)

    def test_contacts_and_groups(self):
        img = self.img
        rows = (("TG 9", 9, 0, 1), ("TG 91", 91, 0, 0), ("Test Private", 7654321, 1, 1))
        for i, (name, did, ctype, res) in enumerate(rows):
            off = 0xA7620 + i * 24
            self.assertEqual(text(img[off:off + 16]), name)
            self.assertEqual(bcd(int.from_bytes(img[off + 16:off + 20], "big")), did)
            self.assertEqual((img[off + 20], img[off + 23]), (ctype, res))
        self.assertEqual(img[0xA7620 + 3 * 24], 0xFF)
        self.assertEqual(img[0xAD620], 3)
        self.assertEqual(img[0xAD621], 0)
        off = 0xAD620 + 128
        self.assertEqual(text(img[off:off + 16]), "Local")
        self.assertEqual(list(struct.unpack_from("<4H", img, off + 16)), [1, 2, 0, 0])

    def test_nv_settings(self):
        size = ctypes.sizeof(NvSettings)
        self.assertLessEqual(size, 0x100)
        nv = NvSettings.from_buffer_copy(self.img[0x604B:0x604B + size])
        self.assertEqual(nv.magicNumber, 0x4780)
        self.assertEqual(nv.backlight[0], 100)
        self.assertEqual(nv.c[8], 0)

    def test_errors(self):
        bad = self.dir / "bad.json"
        bad.write_text(json.dumps({"channels": [{"number": 2000, "name": "x"}]}))
        r = run_gen(bad, self.dir / "o.bin")
        self.assertEqual(r.returncode, 1)
        self.assertIn("out of range", r.stderr)
        self.assertEqual(run_gen(self.dir / "nope.json", self.dir / "o.bin").returncode, 1)


if __name__ == "__main__":
    unittest.main()
