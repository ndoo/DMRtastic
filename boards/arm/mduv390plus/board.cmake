# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
#
# `west flash` is NOT supported for this board.
# The TYT proprietary bootloader requires a custom unlock sequence and
# XOR encoding that no built-in Zephyr runner provides, and Zephyr 4.4's
# runner system does not support out-of-tree plugins.
#
# Flash with:
#   python3 tools/flash_tyt_dfu.py build/zephyr/zephyr.bin
#
# dfu-util is registered here only so CMake generates runners.yaml without
# errors. At runtime it will fail (bootloader silently discards unencoded
# writes), which is the expected behaviour — use the script above instead.

board_set_flasher_ifnset(dfu-util)
board_runner_args(dfu-util "--pid=0483:df11" "--alt=0" "--dfuse")
board_finalize_runner_args(dfu-util)
