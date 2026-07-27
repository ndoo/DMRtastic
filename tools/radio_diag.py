#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
#
# Systematic FM RX diagnostic tool.
# Connects to the DMRtastic radio shell over USB CDC, probes registers,
# and walks through a structured gain/routing sweep to isolate why voice
# is not audible.
#
# Usage:
#   python3 tools/radio_diag.py [/dev/cu.usbmodemXXX]
#
# Requires: pip install pyserial

import argparse
import glob
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Install pyserial first:  pip install pyserial")


# ---------------------------------------------------------------------------
# Port detection
# ---------------------------------------------------------------------------

def find_port():
    for pattern in ('/dev/cu.usbmodem*', '/dev/ttyACM*', '/dev/ttyUSB*'):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


# ---------------------------------------------------------------------------
# Console interface
# ---------------------------------------------------------------------------

class RadioConsole:
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=0.5)
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def _send(self, cmd):
        self.ser.reset_input_buffer()
        self.ser.write((cmd + '\r\n').encode())
        time.sleep(0.2)
        raw = b''
        deadline = time.monotonic() + 0.8
        while time.monotonic() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                raw += chunk
                if b'\n' in raw:
                    break
        return raw.decode(errors='replace').strip()

    def at_read(self, reg):
        """Returns (hi, lo) bytes or (None, None) on error."""
        resp = self._send(f'at r {reg:02x}')
        m = re.search(r'=\s*([0-9A-Fa-f]{4})', resp)
        if m:
            v = int(m.group(1), 16)
            return v >> 8, v & 0xFF
        return None, None

    def at_write(self, reg, hi, lo):
        resp = self._send(f'at w {reg:02x} {hi:02x} {lo:02x}')
        return 'OK' in resp

    def hc_read(self, page, reg):
        """Returns byte value or None on error."""
        resp = self._send(f'hc r {page:02x} {reg:02x}')
        m = re.search(r'=\s*([0-9A-Fa-f]{2})\b', resp)
        return int(m.group(1), 16) if m else None

    def hc_write(self, page, reg, val):
        resp = self._send(f'hc w {page:02x} {reg:02x} {val:02x}')
        return 'OK' in resp

    def rssi(self):
        """Returns (noise, signal) bytes or (None, None)."""
        resp = self._send('rssi')
        m = re.search(r'noise=([0-9A-Fa-f]{2}).*signal=([0-9A-Fa-f]{2})', resp)
        if m:
            return int(m.group(1), 16), int(m.group(2), 16)
        return None, None


# ---------------------------------------------------------------------------
# UX helpers
# ---------------------------------------------------------------------------

def section(title):
    print()
    print('=' * 56)
    print(f'  {title}')
    print('=' * 56)


def wait_enter(msg='Press Enter to continue'):
    input(f'\n  >>> {msg} ...')


def ask_heard():
    while True:
        ans = input('      Do you hear voice? [y/n]: ').strip().lower()
        if ans in ('y', 'yes'):
            return True
        if ans in ('n', 'no'):
            return False
        print('      (y or n please)')


def rssi_burst(radio, count=8, interval=0.4):
    readings = []
    for _ in range(count):
        n, s = radio.rssi()
        if n is not None:
            readings.append((n, s))
        time.sleep(interval)
    return readings


def rssi_avg(readings):
    if not readings:
        return None, None
    return (
        sum(r[0] for r in readings) / len(readings),
        sum(r[1] for r in readings) / len(readings),
    )


# ---------------------------------------------------------------------------
# Diagnostic phases
# ---------------------------------------------------------------------------

def phase_register_dump(radio):
    section('Phase 1: Register baseline dump')

    at_regs = [
        (0x30, 'CTRL'),
        (0x34, 'RX_DIG_GAIN'),
        (0x40, 'AUDIO_PATH'),
        (0x41, 'VOICE_GAIN'),
        (0x44, 'VOL'),
        (0x58, 'FILTERS'),
        (0x1B, 'RSSI_RAW'),
        (0x15, 'IF_TUNE'),
        (0x32, 'AGC_TARGET'),
    ]
    print()
    print('  AT1846S:')
    for addr, name in at_regs:
        hi, lo = radio.at_read(addr)
        if hi is not None:
            print(f'    0x{addr:02X}  {name:15s}  {hi:02X}{lo:02X}')
        else:
            print(f'    0x{addr:02X}  {name:15s}  ERR')

    hc_regs = [
        (0x04, 0x36, 'FM_FEED'),
        (0x04, 0x37, 'LINEOUT2_GAIN'),
        (0x04, 0xE0, 'CODEC'),
        (0x04, 0x34, 'AUDIO_FILTER'),
        (0x04, 0x26, 'ADC_CTRL'),
    ]
    print()
    print('  HR-C6000:')
    for page, addr, name in hc_regs:
        val = radio.hc_read(page, addr)
        if val is not None:
            print(f'    p{page:02X}:0x{addr:02X}  {name:15s}  {val:02X}')
        else:
            print(f'    p{page:02X}:0x{addr:02X}  {name:15s}  ERR')


def phase_rssi(radio):
    section('Phase 2: RSSI — confirm carrier reaches the chip')
    print()
    print('  Reading baseline RSSI (no TX) ...')
    baseline = rssi_burst(radio)
    avg_n, _ = rssi_avg(baseline)
    if avg_n is None:
        print('  ERR: could not read RSSI — check connection')
        return None, None
    print(f'  Baseline noise avg: {avg_n:.1f}')

    wait_enter('Start TX on PMR446 ch1 from the other radio, then press Enter')
    print('  Reading RSSI during TX ...')
    tx_rdgs = rssi_burst(radio)
    tx_n, _ = rssi_avg(tx_rdgs)
    if tx_n is None:
        print('  ERR: could not read RSSI during TX')
        return avg_n, None

    delta = avg_n - tx_n
    print(f'  TX noise avg: {tx_n:.1f}  |  delta: {delta:+.1f}')
    if delta > 15:
        print('  RESULT: strong quieting — carrier received well')
    elif delta > 5:
        print('  RESULT: moderate quieting — carrier received but weak/off-freq')
    else:
        print('  RESULT: barely any quieting — carrier may not be reaching chip')
    return avg_n, tx_n


def phase_voice_gain(radio):
    section('Phase 3: AT1846S voice gain (0x41) sweep')
    print('  Sweeping post-demod voice gain while TX is running.')
    print('  Keep TX active for this whole phase.\n')

    # save original
    orig_hi, orig_lo = radio.at_read(0x41)

    settings = [
        (0x44, 0x31, 'default  0x4431'),
        (0x7F, 0xFF, 'max      0x7FFF'),
        (0x60, 0xFF, 'mid-high 0x60FF'),
    ]

    wait_enter('Start TX and press Enter to begin sweep')
    found = False
    for hi, lo, label in settings:
        radio.at_write(0x41, hi, lo)
        print(f'\n  [0x41 = {hi:02X}{lo:02X}  {label}]')
        print('  Listen for ~2 seconds ...')
        time.sleep(2.0)
        if ask_heard():
            print(f'  *** VOICE HEARD — 0x41={hi:02X}{lo:02X} ({label}) ***')
            found = True
            break

    if not found:
        print('\n  No voice heard with any 0x41 setting.')

    # restore
    if orig_hi is not None:
        radio.at_write(0x41, orig_hi, orig_lo)
    return found


def phase_rx_dig_gain(radio):
    section('Phase 4: AT1846S RX digital gain (0x34) sweep')
    print('  Sweeping pre-demod RX digital gain. Keep TX active.\n')

    orig_hi, orig_lo = radio.at_read(0x34)

    settings = [
        (0x2B, 0x89, 'default  0x2B89'),
        (0x7B, 0x89, 'boosted  0x7B89'),
        (0x7F, 0x89, 'max      0x7F89'),
    ]

    wait_enter('Keep TX running, press Enter to begin sweep')
    found = False
    for hi, lo, label in settings:
        radio.at_write(0x34, hi, lo)
        print(f'\n  [0x34 = {hi:02X}{lo:02X}  {label}]')
        print('  Listen for ~2 seconds ...')
        time.sleep(2.0)
        if ask_heard():
            print(f'  *** VOICE HEARD — 0x34={hi:02X}{lo:02X} ({label}) ***')
            found = True
            break

    if not found:
        print('\n  No voice heard with any 0x34 setting.')

    if orig_hi is not None:
        radio.at_write(0x34, orig_hi, orig_lo)
    return found


def phase_deemph(radio):
    section('Phase 5: De-emphasis toggle (0x58 bit 5)')
    print('  Toggling de-emphasis between AT1846S and HRC6000. Keep TX active.\n')

    orig_hi, orig_lo = radio.at_read(0x58)

    settings = [
        (0x9C, 0x85, 'de-emph on AT1846S  (current 0x9C85)'),
        (0xBC, 0x85, 'de-emph off / HRC6000 (0xBC85)'),
        (0x9C, 0xF5, 'de-emph on + extra filters (0x9CF5)'),
    ]

    wait_enter('Keep TX running, press Enter to begin sweep')
    found = False
    for hi, lo, label in settings:
        radio.at_write(0x58, hi, lo)
        print(f'\n  [0x58 = {hi:02X}{lo:02X}  {label}]')
        print('  Listen for ~2 seconds ...')
        time.sleep(2.0)
        if ask_heard():
            print(f'  *** VOICE HEARD — 0x58={hi:02X}{lo:02X} ({label}) ***')
            found = True
            break

    if not found:
        print('\n  No voice with either de-emphasis setting.')

    if orig_hi is not None:
        radio.at_write(0x58, orig_hi, orig_lo)
    return found


def phase_lineout_gain(radio):
    section('Phase 6: HR-C6000 LineOut2 gain (p04:0x37)')
    print('  Sweeping HR-C6000 output gain. Keep TX active.\n')

    orig = radio.hc_read(0x04, 0x37)
    print(f'  Current p04:0x37 = {orig:02X}' if orig is not None else '  Could not read p04:0x37')

    # HR-C6000 LineOut2 gain encoding: val = 0x80 | (0x40 if gain>0 else 0) | (gain & 0x1F)
    # 0x9E = -2 dB, 0x80 = 0 dB, 0xC0 = +0 dB positive, 0xFF = +31 dB
    settings = [
        (0x9E, '-2 dB (current default)'),
        (0x80, ' 0 dB'),
        (0xC0, '+0 dB positive encoding'),
        (0xFF, '+31 dB (max)'),
        (0xC8, '+8 dB'),
    ]

    wait_enter('Keep TX running, press Enter to begin sweep')
    found = False
    for val, label in settings:
        radio.hc_write(0x04, 0x37, val)
        print(f'\n  [p04:0x37 = {val:02X}  {label}]')
        print('  Listen for ~2 seconds ...')
        time.sleep(2.0)
        if ask_heard():
            print(f'  *** VOICE HEARD — p04:0x37={val:02X} ({label}) ***')
            found = True
            break

    if not found:
        print('\n  No voice with any LineOut2 gain setting.')

    if orig is not None:
        radio.hc_write(0x04, 0x37, orig)
    return found


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='DMRtastic FM RX diagnostic tool')
    parser.add_argument('port', nargs='?',
                        help='Serial port (auto-detected if omitted)')
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit('No CDC serial port found. Connect the radio and try again.')

    print(f'Connecting to {port} ...')
    radio = RadioConsole(port)
    print('Connected.\n')
    print('This tool will walk through systematic register sweeps.')
    print('You will be prompted when TX is needed.')

    phase_register_dump(radio)
    phase_rssi(radio)

    print()
    print('Starting audio diagnostics. Keep the other radio ready to TX.')

    v3 = phase_voice_gain(radio)
    v4 = phase_rx_dig_gain(radio)
    v5 = phase_deemph(radio)
    v6 = phase_lineout_gain(radio)

    section('Summary')
    print(f'  Voice gain (0x41) sweep:     {"HEARD" if v3 else "not heard"}')
    print(f'  RX digital gain (0x34) sweep:{"HEARD" if v4 else "not heard"}')
    print(f'  De-emphasis (0x58) toggle:   {"HEARD" if v5 else "not heard"}')
    print(f'  LineOut2 gain (p04:0x37):    {"HEARD" if v6 else "not heard"}')
    print()
    if not any([v3, v4, v5, v6]):
        print('  No voice heard in any test.')
        print('  Likely causes:')
        print('  - AT1846S analog output noise floor masks demodulated audio')
        print('  - FM feedthrough path in HRC6000 not carrying voice band')
        print('  - Possible IF tuning issue (reg 0x15) or AGC suppression')
    print()


if __name__ == '__main__':
    main()
