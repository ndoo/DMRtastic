# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dmrtastic)

FILE(GLOB app_sources src/*.c)
FILE(GLOB ui_sources
    src/ui/*.c
    src/ui/screens/*.c
    src/ui/overlays/*.c
)
target_sources(app PRIVATE ${app_sources} ${ui_sources})
target_include_directories(app PRIVATE src)
