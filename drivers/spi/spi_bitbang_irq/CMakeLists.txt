# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

if(CONFIG_SPI_BITBANG_IRQ)
  zephyr_library()
  zephyr_library_sources(spi_bitbang_irq.c)
endif()
