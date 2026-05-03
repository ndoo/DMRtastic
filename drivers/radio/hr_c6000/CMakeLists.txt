# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

if(CONFIG_RADIO_HR_C6000)
  zephyr_library()
  zephyr_library_sources(hr_c6000.c)
endif()
