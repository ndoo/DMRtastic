# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

Import("env")

try:
    import usb  # noqa: F401
except ImportError:
    env.Execute("$PYTHONEXE -m pip install pyusb")
