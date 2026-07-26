# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

# Board definition is under the project root boards/ directory
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# Out-of-tree HX8353E driver (remove once upstream PR merges into framework-zephyr)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../drivers/hx8353e)

# Out-of-tree radio stack: IRQ-safe SPI bus, AT1846S transceiver, HR-C6000
# baseband, and the radio manager subsystem.
list(APPEND ZEPHYR_EXTRA_MODULES
	${CMAKE_CURRENT_LIST_DIR}/../drivers/spi/spi_bitbang_irq
	${CMAKE_CURRENT_LIST_DIR}/../drivers/radio/at1846s
	${CMAKE_CURRENT_LIST_DIR}/../drivers/radio/hr_c6000
	${CMAKE_CURRENT_LIST_DIR}/../subsys/radio
)

# Out-of-tree input driver: keyboard matrix sharing GPIOs with the LCD bus.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../drivers/kbd_matrix_shared_bus)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dmrtastic)

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)

file(GLOB app_sources ../src/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE ../src)
