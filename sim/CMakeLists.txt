# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_include_directories(include)

zephyr_library_sources(drivers/sim_ctl.c drivers/sim_png.c)
target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/drivers/sim_host.c)

zephyr_library_sources_ifdef(CONFIG_SIM_PWM drivers/sim_pwm.c)
zephyr_library_sources_ifdef(CONFIG_SIM_AT1846S drivers/sim_at1846s.c)
zephyr_library_sources_ifdef(CONFIG_SIM_HR_C6000 drivers/sim_hr_c6000.c)
zephyr_library_sources_ifdef(CONFIG_SIM_FLASH drivers/sim_flash.c)
zephyr_library_sources_ifdef(CONFIG_SIM_WDT drivers/sim_wdt.c)
zephyr_library_sources_ifdef(CONFIG_SIM_GPIO drivers/sim_gpio.c)
zephyr_library_sources_ifdef(CONFIG_SIM_LCD drivers/sim_hx8353e.c)
zephyr_library_sources_ifdef(CONFIG_SIM_LCD_WIRE drivers/sim_lcd_bus.c)
zephyr_library_sources_ifdef(CONFIG_SIM_INPUT drivers/sim_input.c)
zephyr_library_sources_ifdef(CONFIG_SIM_GPS drivers/sim_gps.c)
zephyr_library_sources_ifdef(CONFIG_SIM_CDC_UART drivers/sim_cdc_uart.c)
