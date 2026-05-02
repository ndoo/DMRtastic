# DMRtastic

Open-source [Zephyr RTOS](https://zephyrproject.org/) firmware for the **TYT MD-UV390 PLUS** 10W GPS dual-band DMR handheld radio.

## Status

Early bring-up. USB serial logging, the display driver, and the watchdog are working. All hardware peripherals are mapped in the device tree; application-level drivers for audio, RF, storage, and GPS are not yet implemented.

## Hardware

| Component | Details |
|---|---|
| **SoC** | STM32F405VGT6 (ARM Cortex-M4, 168 MHz rated - 72 MHz configured, 192 KB SRAM, 1 MB Flash) |
| **Radio** | AT1846S dual-band VHF/UHF transceiver (I2C) |
| **Baseband** | HR_C6000 DMR codec (SPI) |
| **Display** | Himax HX8353-E 160×128 TFT (MIPI DBI 8080 parallel) |
| **GPS** | UART, 9600 baud |
| **Storage** | 2× W25Q SPI NOR flash (codeplug + voice prompts) |
| **Audio** | I2S3, microphone with power control |
| **Input** | 3-row keypad matrix, rotary encoder, PTT buttons |

## Peripheral Status

| Peripheral | Interface | Status | Notes |
|---|---|---|---|
| HX8353E display | MIPI DBI 8080 (bitbang) | ✅ | Driver in `drivers/hx8353e/` (in-repo until [#108055](https://github.com/zephyrproject-rtos/zephyr/pull/108055) merges); LVGL integration working |
| USB CDC-ACM serial | USB OTG FS | ✅ | Zephyr next-gen USBD stack; pre-DTR log buffering |
| Watchdog (IWDG) | — | ✅ | 50 ms feed period; green LED heartbeat at 1 Hz |
| Green / Red LEDs | GPIO | 🔧 | Zephyr LED API available; red LED not yet used |
| I2S3 audio | I2S | 🔧 | STM32 I2S driver upstream; I2S3_ext for full-duplex RX |
| GPS | USART1, 9600 baud | 🔧 | Zephyr GNSS subsystem (`nmea_generic`) available |
| Keypad matrix | GPIO (3 rows) | 🔧 | `gpio-kbd-matrix` driver upstream; LVGL input node registered |
| Rotary encoder | GPIO quadrature | 🔧 | `gpio_qdec` upstream; LVGL relative-axis input registered; `invert-direction` property pending upstream ([#108010](https://github.com/zephyrproject-rtos/zephyr/pull/108010)) |
| Volume knob | ADC1 ch0 (PA0) | 🔧 | STM32 ADC driver upstream; mapped to LVGL `ABS_THROTTLE` |
| DAC1 | PA4 | 🔧 | STM32 DAC driver upstream; intended for CTCSS/tone generation |
| Codeplug flash | SPI1 (W25Q128) | 🔧 | `jedec,spi-nor` driver upstream; 4.5 MB/s |
| Voice-prompt flash | SPI2 (W25Q) | 🔧 | `jedec,spi-nor` upstream; SPI Mode 2 |
| RTC | LSE 32.768 kHz | 🔧 | `stm32_rtc` driver upstream |
| AT1846S RF transceiver | I2C3 | 🔲 | No upstream driver; band select, audio mux, PA control GPIOs mapped |
| HR_C6000 DMR baseband | SPI (bitbang) | 🔲 | No upstream driver; TS/SYS/RFTX interrupt lines mapped |
| Beeper | GPIO/PWM (PC8) | 🔲 | |
| Microphone | PA13 power enable | 🔲 | |

**Legend:** ✅ implemented &nbsp; 🔧 device tree only — upstream Zephyr driver exists &nbsp; 🔲 device tree only — needs custom driver

## Build

```sh
pio run -e mduv390plus
```

The first run downloads the Zephyr framework package (~1 GB).

## Flash

Put the device into DFU mode **before** running the upload command: hold **PTT + SK1** while powering on.

`west flash` is not supported — the TYT bootloader requires a proprietary unlock/XOR sequence handled by `tools/flash_tyt_dfu.py`.

```sh
pio run -e mduv390plus --target upload
```

## Upstream Contributions

| Contribution | PR | Status | In-repo implementation |
|---|---|---|---|
| HX8353E display driver | [zephyrproject-rtos/zephyr#108055](https://github.com/zephyrproject-rtos/zephyr/pull/108055) | Pending review | `drivers/hx8353e/` — will be removed once merged |
| `gpio_qdec`: add `invert-direction` property | [zephyrproject-rtos/zephyr#108010](https://github.com/zephyrproject-rtos/zephyr/pull/108010) | Pending review | Not yet implemented — planned post-merge |

## License

[MIT](LICENSE)
