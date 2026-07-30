# DMRtastic

Open-source [Zephyr RTOS](https://zephyrproject.org/) firmware for the **TYT MD-UV390 PLUS** 10W GPS dual-band DMR handheld radio, built on an STM32F405VGT6 (ARM Cortex-M4, 168 MHz rated / 72 MHz configured, 192 KB SRAM, 1 MB Flash). PRs and issue reports welcome.

## Status

Early bring-up on real hardware.

<table>
<thead>
<tr><th>Peripheral</th><th>Interface</th><th>Driver</th><th>Hardware</th><th>Firmware</th><th>Notes</th></tr>
</thead>
<tbody>
<tr><th colspan="6" align="left">RF / DMR</th></tr>
<tr><td>AT1846S transceiver driver</td><td>I2C3</td><td><code>auctus,at1846s</code></td><td>🟢</td><td>🟢</td><td>FM RX (AT1846S → HR-C6000 → speaker) — end-to-end functional; DCS squelch stub pending</td></tr>
<tr><td>HR-C6000 baseband driver</td><td>SPI bitbang-IRQ</td><td><code>hongrui,hr-c6000</code></td><td>🟢</td><td>🟢</td><td>FM RX audio path functional (see above)</td></tr>
<tr><td>PTT</td><td>GPIO keys (PE11/PE12)</td><td><code>gpio-keys</code></td><td>⭐</td><td>🟡</td><td>Wired to the UI's TX indicator only; no RF transmit yet</td></tr>
<tr><td>DMR TX / AMBE / synchronisation</td><td>—</td><td>—</td><td>—</td><td>🔴</td><td></td></tr>
<tr><th colspan="6" align="left">Display</th></tr>
<tr><td>HX8353E display driver</td><td>MIPI DBI 8080 bitbang</td><td><code>himax,hx8353e</code></td><td>⭐[^1]</td><td>🟢</td><td>LVGL UI — FM VFO, Settings (Radio/Display/Info tabs), quick-menu overlay</td></tr>
<tr><td>Backlight</td><td>PWM (TIM3_CH1/PC6)</td><td><code>st,stm32-pwm</code></td><td>⭐</td><td>🟢</td><td>Brightness, backlight-off level, screen timeout, screen invert — Settings-driven</td></tr>
<tr><th colspan="6" align="left">Input</th></tr>
<tr><td>Keypad matrix driver</td><td>GPIO (3 rows)</td><td><code>gpio-kbd-matrix-shared-bus</code></td><td>🟢</td><td>🟢</td><td>Keypad → LVGL navigation</td></tr>
<tr><td>Rotary encoder</td><td>GPIO quadrature</td><td><code>gpio-qdec</code></td><td>⭐[^2]</td><td>🟢</td><td>Volume/menu-value adjustment</td></tr>
<tr><td>Volume knob</td><td>ADC1 ch0 (PA0)</td><td><code>st,stm32-adc</code></td><td>⭐</td><td>🟢</td><td>Hysteresis-gated reading, audio-taper display curve</td></tr>
<tr><th colspan="6" align="left">Storage</th></tr>
<tr><td>Codeplug flash</td><td>SPI1 (W25Q128)</td><td><code>jedec,spi-nor</code></td><td>⭐</td><td>🟢<br>🟡</td><td>Read/decode (<code>src/codeplug.c</code>), hardware-validated<br>Write — call chain wired end-to-end, flash write stubbed (<code>-ENOTSUP</code>) pending CPS protocol support</td></tr>
<tr><td>Voice-prompt flash</td><td>SPI2 (W25Q)</td><td><code>jedec,spi-nor</code></td><td>⭐</td><td>⚪</td><td></td></tr>
<tr><th colspan="6" align="left">Power</th></tr>
<tr><td>Battery voltage</td><td>ADC1 ch1 (PA1)</td><td><code>st,stm32-adc</code></td><td>⭐</td><td>🟢</td><td>OCV curve, status-bar %/V display</td></tr>
<tr><td>DAC1</td><td>PA4</td><td><code>st,stm32-dac</code></td><td>⭐</td><td>⚪</td><td>CTCSS/tone generation</td></tr>
<tr><th colspan="6" align="left">Connectivity</th></tr>
<tr><td>USB CDC-ACM</td><td>USB OTG FS</td><td><code>zephyr,cdc-acm-uart</code></td><td>⭐</td><td>🟢</td><td>Non-blocking ring buffer, pre-DTR replay</td></tr>
<tr><td>GPS UART</td><td>USART1, 9600 baud</td><td><code>st,stm32-usart</code></td><td>⭐</td><td>🔴</td><td>Fix acquisition / NMEA parsing</td></tr>
<tr><th colspan="6" align="left">System</th></tr>
<tr><td>Watchdog</td><td>IWDG</td><td><code>st,stm32-watchdog</code></td><td>⭐</td><td>🟢</td><td>Feed loop, green-LED heartbeat</td></tr>
<tr><td>Green / Red LEDs</td><td>GPIO</td><td><code>gpio-leds</code></td><td>⭐</td><td>🟢<br>🟡</td><td>Green: 1 Hz heartbeat<br>Red: available, no indicator assigned yet</td></tr>
<tr><td>RTC</td><td>LSE 32.768 kHz</td><td><code>st,stm32-rtc</code></td><td>⭐</td><td>🟡</td><td></td></tr>
<tr><td>I2S3 audio</td><td>I2S3_ext for full-duplex RX</td><td><code>st,stm32-i2s</code></td><td>⭐</td><td>🔴</td><td>Streaming (mic capture, DMR TX/RX audio)</td></tr>
<tr><td>Beeper</td><td>GPIO (PC8)</td><td><code>st,stm32-gpio</code></td><td>🟡[^3]</td><td>⚪</td><td>Tones</td></tr>
<tr><td>Microphone</td><td>PA13 power enable</td><td><code>st,stm32-gpio</code></td><td>🟡[^4]</td><td>🔴</td><td>Capture (DMR TX audio input)</td></tr>
</tbody>
</table>

[^1]: [Zephyr #108055](https://github.com/zephyrproject-rtos/zephyr/pull/108055) — pending next release.
[^2]: [Zephyr #108010](https://github.com/zephyrproject-rtos/zephyr/pull/108010) — pending next release.
[^3]: Toggled directly; no PWM/tone-capable DT config yet.
[^4]: Power-enable pin only; capture path (I2S3) not wired to this use.

Legend: ⭐ implemented with upstream driver &nbsp; 🟢 implemented with out-of-tree driver &nbsp; 🟡 scaffolded &nbsp; 🔴 planned &nbsp; ⚪ future

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

## User Interface

An LVGL UI runs over the shared keypad/LCD data bus, driven by Up/Down/Enter/Back (keypad matrix) and CW/CCW (rotary encoder):

- **FM VFO** — the default screen; functional receive display.
- **Settings** — a persistent `Radio` / `Display` / `Info` tabview (`src/ui/screens/screen_settings.c`), built once at boot and never destroyed. Squelch, bandwidth, VFO step, brightness, backlight-off level, screen timeout, screen invert, visual-volume, battery-unit, and all-LEDs toggles are live; items blocked on missing subsystems (GPS, RTC, contacts, DMR) show as N/A. Settings state is centralized in `src/radio_settings.c`, seeded from the codeplug's nv-settings block where a field mapping exists.
- **Quick-menu overlay** — SK1 long-press.

Two-axis navigation convention, used consistently across every tabview: Up/Down always move focus (tab cycling, then row selection within a tab); the encoder never moves focus and instead adjusts whatever's currently focused.

## Shell Commands

Interactive shell over USB CDC-ACM (`src/shell_radio.c`):

| Command | Purpose |
|---|---|
| `at r/w` | Raw AT1846S register read/write |
| `hc r/w` | Raw HR-C6000 register read/write |
| `rssi` | Live RSSI reading |
| `cp list` | List known codeplug flash regions |
| `cp dump <addr> <len>` | Raw codeplug flash hexdump |
| `cp region <name> [idx]` | Typed decode of one codeplug region |
| `cp settings` | On-flash nv-settings block + magic number |
| `cp info` | JEDEC ID, device info, calibration sanity check |
| `rtc r/w` | Read/set the hardware RTC date-time |

`tools/radio_diag.py` drives the `at`/`hc`/`rssi` commands from the host for automated register sweeps and gain/filter characterization.

## Upstream Contributions

| Contribution | PR | Status | In-repo implementation |
|---|---|---|---|
| HX8353E display driver | [zephyrproject-rtos/zephyr#108055](https://github.com/zephyrproject-rtos/zephyr/pull/108055) | Merged to `main` 2026-06-12, not yet in a tagged release | Vendored copy in `drivers/hx8353e/`, synced to the merged content — will be removed once the pinned Zephyr version includes it |
| `gpio_qdec`: add `invert-direction` property | [zephyrproject-rtos/zephyr#108010](https://github.com/zephyrproject-rtos/zephyr/pull/108010) | Merged to `main` 2026-05-04, not yet in a tagged release | `invert-direction` DT property not yet adopted — will be enabled once the pinned Zephyr version includes it |

## Design Notes

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

Logging over USB is non-blocking: `cdc_log_out_func()` only writes
into a ring buffer (drop-oldest when full); a dedicated drain thread
(`cdc_drain_thread_fn()`) flushes it to the CDC ACM UART via
`uart_poll_out()`, which yields via `k_msleep(1)` when the TX FIFO is full
so the drain thread never spins. If the host stops draining, only the
drain thread stalls — logs accumulate and the oldest are dropped, but
everything else keeps running. Pre-DTR, the drain thread parks on a 100 ms
poll so boot-time logs are buffered for replay once the host opens the
port.

### Shared keypad/LCD bus (`drivers/kbd_matrix_shared_bus/`, `src/display.c`)

The keypad matrix scan borrows the same 8 GPIOs as the LCD's `lcd_mipi_dbi`
data bus and hands them back before returning, so the scan must run on the
same thread as the LVGL display driver (see
`include/drivers/input/kbd_matrix_shared_bus.h`).

Within the scan itself, two STM32-specific quirks apply:
- **Mode-transition glitch**: switching a pin directly from whatever output
  state it's in to input mode can produce a brief spurious HIGH glitch. The
  driver forces all row pins to output-low uniformly first, lets them
  settle, then switches to input, rather than transitioning them
  individually.
- **No pull resistor on the input transition**: the scan uses `NOPULL` with
  an active-high scheme rather than an internal pull-up, because an
  internal pull-up would source current onto lines also wired to the LCD's
  data input pins — which this shared bus can't tolerate.

### AT1846S I2C access (`drivers/radio/at1846s/`)

The chip is operated over I2C at 400 kHz. Each register is a 16-bit value
addressed by an 8-bit register number; access is two separate bus
transactions (the chip returns stale data on a repeated start). A small
per-bank cache suppresses redundant writes when the requested value matches
the last successful write to the same register/bank.

Bus recovery runs on every boot, to clear any SDA hold left by a transaction
interrupted in a prior boot. `i2c_configure()` is never called afterwards:
on STM32F4 I2C v1, the runtime reconfigure path leaves the event interrupt
disabled — a known silicon/driver errata.

### Volume pot hysteresis (`src/ui/ui_input.c`)

Volume-pot handling operates on the pot's native calibrated ADC reading
(`vol_axis_ch`'s `in-min`/`in-max` span) rather than a rescaled percent, so
the taper LUT and the hardware volume (`src/ui/ui.c`) each round
independently at their own point of use.

The STM32F405's ADC has no hardware oversampler (`zephyr,oversampling`
returns `-ENOTSUP`) and `zephyr,acquisition-time` is already maxed at 480
ticks, so hysteresis is handled at the application layer instead: a new
value is reported only once it moves at least a gate's width — 1% of the
calibrated span, so it tracks `in-min`/`in-max` if retuned — from the last
one applied. `analog-axis`'s own `in-deadzone` doesn't help here; it's a
*center* deadzone for joystick-style inputs, not a slider's full range.

The noise this fixes (CDC log capture showed oscillation between adjacent
steps with the pot held still) occurs near the physical maximum, where the
audio-taper reverse-mapping LUT is steepest. Near either extreme, a step
moving *toward* it bypasses the gate so the ends of the range stay
reachable; a step *away* from an extreme still needs the full gate, so it
doesn't drift back on noise alone.

### Display init failure (`src/display.c`)

LVGL's Zephyr auto-init skips registering a display whose underlying device
failed `device_is_ready()` (e.g. the HX8353E's init returning an error) —
`lv_display_get_default()` then returns `NULL` instead of failing directly.
`lv_theme_default_init()` doesn't handle that and previously crashed the
system with an unhandled fault; the LVGL thread now checks for it and
returns early, so a display-init failure only loses the UI — the radio
keeps receiving independently (see `src/main.c`).

### UI threading & frame lifecycle (`src/ui/ui.c`, `src/ui/ui.h`)

All LVGL object access is confined to the LVGL thread (the one that calls
`ui_tick()` and `lv_timer_handler()`). External contexts post `ui_action_t`
events via `ui_post_action()`; `ui_tick()` drains them each iteration —
this is the only place actions cross from the outside world into LVGL.

Frames (FM VFO, Settings) are created once at `ui_init()` and never
destroyed — navigation hides the current frame and shows another, not
create/destroy churn. `SCREEN_BOOT` is the one exception: a one-shot
transient torn down by `ui_switch_screen()` the first time it's called.

The rotary encoder has no `lv_group` binding: it adjusts whatever's
focused directly via `UI_ACTION_ENCODER_CW`/`CCW` rather than moving focus
itself (see [User Interface](#user-interface) above for the full
navigation convention).

### Volume taper curve (`src/ui/ui.c`)

Hardware volume has no taper correction — the pot's own physical taper
already applies one; the raw reading is linearly rescaled to the 0-100%
the driver API expects. Display percentage, however, is reverse-mapped
through an 11-point piecewise-linear LUT (checkpoints at 0/10, 1/10, ...,
10/10 of the pot's native span) so the *displayed* value reads as
perceptually linear. Which LUT applies is chosen at compile time by the
AT1846S node's `volume-taper` DT property.

The default "audio A" (log) taper models the industry-standard ~10-15%
output at 50% rotation (this board measured ~14% on-hardware) as two linear
segments meeting at that point, rather than a smooth analytic curve —
matching how these pots are manufactured (two overlapping resistive
tracks).

`volume_display_pct()` scales the raw-minus-min offset by 10 *before*
dividing by the span, so the checkpoint index and the in-segment fraction
both come out of one exact calculation without requiring the span itself
to be a multiple of 10 (it isn't: `vol_axis_ch`'s calibrated span is 1936
counts). The final interpolated value is rounded (`DIV_ROUND_CLOSEST`)
rather than truncated, which used to bias every step down by up to just
under one point.

### Settings tabview navigation (`src/ui/screens/screen_settings.c`)

The Settings screen's two-level hierarchy (tabs, then rows within a tab)
requires explicit group-membership management: `lv_tabview_set_active()`
only scrolls the tab container into view, it doesn't hide inactive tabs'
content. An earlier single flat group (tab buttons and the active tab's
rows all navigable together) meant Up/Down's `PREV`/`NEXT` traversal could
wander into off-screen rows from whichever tab wasn't showing. The fix
keeps the two levels from ever sharing group membership: tab buttons are
group members only at the tab level, the active tab's rows only at the row
level, swapped explicitly on descend/ascend.

### Codeplug flash decoding (`src/codeplug.c`, `src/codeplug.h`)

Structs are byte-exact overlays of the on-flash format (region offsets come
from the board DT's codeplug-map), not a parsed/normalized representation —
`codeplug_get_*()` accessors read a region straight into its struct and
BCD-decode scalar fields (frequencies, talkgroup numbers) in place. CSS tone
and lat/lon fields stay raw on the struct and are decoded explicitly via
`codeplug_decode_css()`/`codeplug_decode_latlon()`, since not every caller
needs them unpacked.

Empty slots are exposed via `*_is_in_use()` helpers rather than requiring
callers to know the on-flash sentinel convention (`name[0] == 0xFF`).
Zone format (modern 80-channel/176-byte vs. legacy 16-channel/48-byte) is
runtime-detected rather than address-based.

The write path (`codeplug_set_nv_settings()` → `codeplug_write()`) is
implemented end-to-end except the actual flash write, which stays a logged
`-ENOTSUP` no-op until CPS-side write support exists — this lets the whole
path be exercised and confirmed on hardware ahead of that work.

### Settings config manager (`src/radio_settings.c`, `src/radio_settings.h`)

Radio/Display settings (squelch, bandwidth, VFO step, brightness,
backlight-off, screen timeout, invert, visual-volume, battery-unit, LEDs)
live in one place instead of as independent statics scattered through the
UI. `settings_init()` seeds each field from the codeplug's nv-settings
block where a mapping exists (magic-gated), falling back to firmware
defaults otherwise; `settings_subscribe()` lets a consumer (currently the
UI, eventually the radio subsystem) react to a change instead of the
setter applying driver calls inline.

`settings_get_range()` is the single source of truth for numeric-field
bounds, consulted by both the setters' internal clamp and the UI's own
step math (e.g. the backlight-off dead-zone, screen-timeout ceiling) —
`settings_init()` routes the raw codeplug value through the same clamp
the setters use, after a codeplug-sourced 4% brightness value (below the
PWM's usable floor) once made the display unreadable on first boot with
real codeplug data.

### Battery reading (`src/battery.c`, `src/battery.h`)

Pack voltage is read via the native voltage-divider ADC DT binding
(`adc_raw_to_millivolts_dt()` gives pin mV; the divider's `output-ohms`/
`full-ohms` ratio scales that back up to pack mV — the ADC API doesn't
apply the divider itself). Percentage comes from an 11-point single-cell
LiPo open-circuit-voltage curve (10% steps), looked up on
`pack_mV / BATTERY_CELL_COUNT` and linearly interpolated between
checkpoints.

Sampling is self-throttled to 1 Hz inside `battery_poll()` — safe to call
every tick — and the first sample after channel setup waits out a 400 ms
settle delay (5× the divider's RC time constant against a worst-case 10 µF
sense cap) so the initial reading isn't taken mid-transient.
