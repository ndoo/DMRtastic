# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_INPUT_KBD_MATRIX_SHARED_BUS)
  zephyr_library()
  zephyr_library_sources(kbd_matrix_shared_bus.c)
endif()
