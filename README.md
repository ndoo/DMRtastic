# DMRtastic

Open-source [Zephyr RTOS](https://zephyrproject.org/) firmware for the **TYT MD-UV390 PLUS** 10W GPS dual-band DMR handheld radio.

## Status

Early bring-up. USB serial logging, the display driver, and the watchdog are working. In-repo drivers for the AT1846S transceiver and HR-C6000 baseband codec are implemented; FM RX audio (AT1846S → HR-C6000 → speaker) is end-to-end functional. Storage, GPS, and DMR baseband (TX path, AMBE codec, synchronisation) are not yet implemented.

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
| AT1846S RF transceiver | I2C3 | ✅ | In-repo driver (`drivers/radio/at1846s/`); FM RX functional; DCS squelch stub pending |
| HR_C6000 DMR baseband | SPI (bitbang-IRQ) | ✅ | In-repo driver (`drivers/radio/hr_c6000/`); FM RX audio path functional; DMR state machine pending |
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

## Design Notes

Rationale that used to live as long inline comments, moved here to keep the
source terse. Each subsection maps to the file(s) it explains.

### HR-C6000 SPI framing (`drivers/radio/hr_c6000/`)

The SPI control bus protocol is a 3-byte transaction `{ page, reg, value }`
with CS asserted across all three bytes. Bursts append more value bytes
after `{ page, reg }` and the chip auto-increments `reg`.

### SPI bit-bang timing (`drivers/spi/spi_bitbang_irq/`)

Bus speed is set entirely by GPIO toggle overhead; the `frequency` field of
`spi_config` is ignored. On a 72 MHz Cortex-M4 with the STM32 GPIO driver
this comes out to roughly 4 MHz per bit, well within the HR-C6000 SPI
maximum.

### USB CDC-ACM (`src/usb_cdc.c`, `src/usb_cdc.h`)

USB CDC-ACM has no public API and runs entirely in its own thread. The
whole lifecycle (descriptor registration, enumeration wait, DTR handshake,
log-backend handover) lives in `usb_cdc_thread_fn()` so a stalled host or a
misbehaving USB stack can't block the radio control loop, the watchdog
feed, or the UI.

Logging over USB is fully non-blocking: `cdc_log_out_func()` only writes
into a ring buffer (drop-oldest when full); a dedicated drain thread
(`cdc_drain_thread_fn()`) flushes it to the CDC ACM UART via
`uart_poll_out()`, which yields via `k_msleep(1)` when the TX FIFO is full
so the drain thread never spins. If the host stops draining, only the
drain thread stalls — logs accumulate and the oldest are dropped, but
everything else keeps running. Pre-DTR, the drain thread parks on a 100 ms
poll so boot-time logs are buffered for replay once the host opens the
port.

## License

[MIT](LICENSE)
