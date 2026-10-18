# Copyright (c) 2026 Andrew Yong <me@ndoo.sg> SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND ZEPHYR_EXTRA_MODULES
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/hx8353e
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/spi/spi_bitbang_irq
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/radio/at1846s
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/radio/hr_c6000
     ${CMAKE_CURRENT_SOURCE_DIR}/subsys/radio
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/kbd_matrix_shared_bus)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dmrtastic)

file(GLOB app_sources src/*.c)
file(GLOB model_sources src/model/*.c)
file(GLOB view_sources src/view/*.c src/view/screens/*.c src/view/overlays/*.c
     src/view/fonts/*.c)
file(GLOB controller_sources src/controller/*.c)
file(GLOB console_sources src/console/*.c)
target_sources(app PRIVATE ${app_sources} ${model_sources} ${view_sources}
                           ${controller_sources} ${console_sources})
zephyr_include_directories(include)
target_include_directories(app PRIVATE src)
