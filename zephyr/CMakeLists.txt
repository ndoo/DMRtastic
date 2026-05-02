# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

# Board definition is under the project root boards/ directory
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# Out-of-tree HX8353E driver (remove once upstream PR merges into framework-zephyr)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../drivers/hx8353e)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dmrtastic)

file(GLOB app_sources ../src/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE ../src)
