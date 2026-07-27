#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
#
# Minimal CDC console capture tool for troubleshooting.
#
# Connects to the DMRtastic radio's USB CDC port (shared by Zephyr LOG
# output and the shell_radio.c command shell) and either:
#   - streams whatever is currently coming out (default), or
#   - sends "reboot" first, to capture a fresh boot log from reset.
#
# Usage:
#   python3 tools/console.py [--port /dev/cu.usbmodemXXX] [--reboot] [--seconds 5]

import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Install pyserial first:  pip install pyserial")


def find_port():
    # macOS can leave a stale /dev/cu.usbmodemNNNNN entry behind after a
    # device resets and re-enumerates under a different number — sorting
    # alphabetically picks the wrong one. Prefer whichever device node was
    # actually created most recently.
    import os

    candidates = []
    for pattern in ('/dev/cu.usbmodem*', '/dev/ttyACM*', '/dev/ttyUSB*'):
        candidates.extend(glob.glob(pattern))
    if not candidates:
        return None
    candidates.sort(key=lambda p: getattr(os.stat(p), 'st_birthtime',
                                          os.stat(p).st_ctime), reverse=True)
    return candidates[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--reboot', action='store_true',
                     help='send "reboot" first, to capture a fresh boot log')
    ap.add_argument('--seconds', type=float, default=5.0,
                     help='how long to capture output for')
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("No CDC ACM port found. Pass --port explicitly.")

    ser = serial.Serial(port, args.baud, timeout=0.1)
    time.sleep(0.3)
    ser.reset_input_buffer()

    buf = b''

    if args.reboot:
        ser.write(b'reboot\r\n')

    # Single tight loop for the whole capture window: on a reboot the port
    # drops and re-enumerates mid-window, possibly on the same or a new
    # path — reopen immediately (no fixed sleep) whenever a read fails, so
    # we don't miss the log backend's post-reset buffered-message flush.
    deadline = time.monotonic() + args.seconds
    cur_port = port
    while time.monotonic() < deadline:
        try:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
        except serial.SerialException:
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            while ser is None and time.monotonic() < deadline:
                cur_port = find_port() or cur_port
                try:
                    ser = serial.Serial(cur_port, args.baud, timeout=0.1)
                except serial.SerialException:
                    pass

    sys.stdout.write(buf.decode(errors='replace'))


if __name__ == '__main__':
    main()
