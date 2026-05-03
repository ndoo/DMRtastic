# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

if(CONFIG_RADIO_MANAGER)
  zephyr_library()
  zephyr_library_sources(radio_mgr.c)
endif()
