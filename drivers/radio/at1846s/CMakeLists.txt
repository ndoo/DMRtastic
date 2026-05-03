# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

if(CONFIG_RADIO_AT1846S)
  zephyr_library()
  zephyr_library_sources(at1846s.c)
endif()
