# BLED display unit — CrowPanel ESP32 Advanced 7.0" (DIS02170A v1.4)

User interface of the **Blood Loss Estimation Device (BLED)**, built on the CrowPanel PlatformIO / ESP-IDF driver.

Board: ELECROW CrowPanel ESP32 Display Advanced 7" — SKU **DIS02170A v1.4**
MCU: **ESP32-S3-WROOM-1-N16R8** (16 MB flash, 8 MB octal PSRAM)
Panel: 800×480 IPS, parallel RGB565 (truncated bus), capacitive touch (GT911)

## What the firmware does

Permanent **status bar** (device, active case with colour-coded blood loss, clock, and UNIT / SD / Wi-Fi / power
indicators) above six tabs:

| Tab | Purpose |
|---|---|
| **Home** | Profile, **Dashboard** (live state of the measurement unit, active case, SD card, power, clock, network; green/amber/red banner), Brightness, Sound, About |
| **New Case** | 4-step wizard: case number → patient baseline Hb → system (optical) baseline → review/start. **Back** works on every step and keeps what you typed. While a case runs the tab shows live pads / cumulative EBL and **End case** (with confirmation) |
| **Records** | Cases stored on the SD card (newest first); tap one for its summary, cumulative-EBL chart and every pad |
| **Clock** | RTC clock + Set Time / Set Date (unchanged) and live cards: case elapsed time, pads measured, blood loss |
| **Calendar**, **Connections** | unchanged (Wi-Fi scan/connect) |

Removed from the original demo: the **Punch Clock** and **Air Quality** tabs, the weather cards, and the backend
pollers that only fed them (`door_poller`, `sensor_poller`, plus the `esp_http_client`/`json` dependencies).

## Hardware setup (read this before the first run)

* Back **DIP switches S1 / S0 = 1 / 1 ("MIC & TF card")**. This connects the SD slot (IO4/IO5/IO6). In this mode the on-board speaker cannot play sound (shared I2S pins); the buzzer still works.
* SD card: **FAT32**. It is used in SPI mode (MOSI IO6, MISO IO4, SCK IO5); the slot's CS is hard-wired, so the
  driver is given a dummy CS pin (IO8, `BLED_SD_PIN_CS_DUMMY`). Hot-plug is supported (retry every 4 s).
* Measurement unit link: UART0-OUT port, display **RX = IO44, TX = IO43**, 115200 8N1 (not UART1: its pins IO19/IO20 are shared with the microphone at DIP 1/1). This is also the console UART, so display logs are off in real mode and the USB-serial cable must be unplugged. Protocol:
  [`docs/BLED_LINK_PROTOCOL.md`](docs/BLED_LINK_PROTOCOL.md). The wire framing code `src/bled_proto.c/.h` is pure C:
  copy it unchanged into the measurement unit firmware.

## Configuration (`src/bled_config.h`)

* `BLED_LINK_SIMULATE` – **1 (default): built-in simulated measurement unit** so everything can be demonstrated
  without wiring (an amber **SIM** badge is always visible, and a *Simulate pad* button appears on the active-case
  panel). Set to **0** to use the real UART link.
* `BLED_EBL_WARN_ML` / `BLED_EBL_ALERT_ML` – colour thresholds for cumulative blood loss (500 / 1000 mL).
  **Placeholders** – the clinical team must set the real values; they only change a colour.
* Case-ID length, pad limit, Hb entry range, SD/UART pins.

## SD record format

One text file per case, `/sdcard/BLED/Cnnnnnnn.CSV` (8.3 name because FATFS long names are disabled; the real case
number is inside). Opens directly in Excel. Append-only after the header, so a power cut can lose at most the last
row. See the comment at the top of `src/bled_storage.h`.

## Regression tests (PC, no hardware)

`make -C test/host` builds and runs the logic tests under AddressSanitizer/UBSan with ESP-IDF-equivalent warnings:
frame format + CRC (incl. fuzzing and an independent Python cross-check), calendar maths, SD record files (power-cut
truncation, hostile input, >200 records), the UART link in real mode, and the case model. Needs only gcc and python3.

## Project layout

```
platformio.ini / CMakeLists.txt / partitions.csv / sdkconfig.*   unchanged build setup (see "Notes" below)
docs/BLED_LINK_PROTOCOL.md      display <-> measurement-unit message set
src/
  app_main.c                    bring-up sequence (now also calls bled_core_init())
  crowpanel_bsp.*  crowpanel_touch.*  stc8h1k28.*  pcf8563.*  wifi_manager.*     board drivers (unchanged)
  bled_config.h                 pins, simulate switch, limits, thresholds
  bled_proto.*                  frame format + CRC-16 + strict parsers (pure C, shared with the unit)
  bled_link.*                   UART link (or simulated unit), events queue
  bled_storage.*                SD mount task + case CSV writing/reading
  bled_core.*                   case/system model: validation, baseline, pads, SD persistence
  bled_time.*                   timestamp helpers
  ui_app.*                      status bar + tab view, drives bled_core_tick() from an LVGL timer
  ui_theme.*  ui_statusbar.*    clinical palette + shared widgets, status bar
  ui_home.c                     Home tab (Profile / Dashboard / Brightness / Sound / About)
  ui_newcase.*  ui_records.*    New Case wizard, Records browser
  ui_clock.*  ui_calendar.*  ui_connections.*
```

## Build & flash

```
pio run -t upload
pio device monitor
```

If PlatformIO's espressif32 package version differs from what's pinned in
`platformio.ini`, adjust it — anything IDF 5.0+ works with this code.

## Pin mapping (from the DIS02170A schematic)

| Signal | GPIO | Signal | GPIO | Signal | GPIO |
|--------|------|--------|------|--------|------|
| HSYNC  | 40   | G2     | 9    | B3     | 21   |
| VSYNC  | 41   | G3     | 10   | B4     | 47   |
| DE     | 42   | G4     | 11   | B5     | 48   |
| PCLK   | 39   | G5     | 12   | B6     | 45   |
| R3     | 7    | G6     | 13   | B7     | 38   |
| R4     | 17   | G7     | 14   |        |      |
| R5     | 18   |        |      |        |      |
| R6     | 3    |        |      |        |      |
| R7     | 46   |        |      |        |      |

Only 5/6/5 bits of R/G/B are routed (the low bits are tied off on the PCB) —
this is normal for this panel family, it's still a full RGB565 16-bit bus,
just missing the least-significant bits per channel.

I2C bus (shared by GT911, PCF8563 RTC, STC8H1K28 IO-extender):
**SDA = IO15, SCL = IO16**. GT911 `INT` is on **IO1**; GT911 `RESET` is
driven by the STC8H1K28, not by the ESP32.

## STC8H1K28 IO-extender command reference (I2C addr `0x30`)

**Your board is hardware v1.4 / firmware v1.3**, confirmed against Elecrow's
own wiki and GitHub README for this exact model. This uses a different,
newer protocol than some older Elecrow docs describe (those cover the
v1.1/v1.2 boards) - all single-byte decimal values, no special sequencing:

| Value (decimal) | Effect |
|---|---|
| `0`–`244` | Backlight brightness: `0` = max, `244` = dimmest |
| `245` | Backlight off |
| `246` | Buzzer on |
| `247` | Buzzer off |
| `248` | Speaker (NS4168 amp) on |
| `249` | Speaker (NS4168 amp) off |

`bsp_display_backlight_set()` maps a UI-friendly level of 0-10 onto this
native 0-244 scale for you (0 = off via the 245 command, 10 = max via 0,
linear in between).

GT911 touch IC is at I2C addr `0x5D`.

## Notes / things to verify on your unit

- **Pixel clock** is set to 16 MHz as a safe starting point for this
  800×480 truncated-RGB565 panel family. If you see noise, tearing, or a
  shifted image, try 12–14 MHz first (`crowpanel_bsp.c` →
  `panel_config.timings.pclk_hz`), then fine-tune porches if needed.
- **I2C driver**: this project uses the new `i2c_master` driver
  (`driver/i2c_master.h`) throughout, matching Elecrow's own reference
  driver pattern for the STC8H1K28 - not the legacy `driver/i2c.h` API.
  Both the STC8H1K28 (`stc8h1k28.c`) and the GT911 touch IO
  (`crowpanel_touch.c`) attach as separate devices on the single shared
  bus created in `bsp_i2c_init()`. Don't mix in legacy `i2c_driver_install()`
  calls anywhere on this bus - the two driver generations can't coexist
  on the same port.
- **LVGL color depth**: the managed `lvgl/lvgl` component defaults to
  16-bit color via Kconfig, matching the RGB565 frame buffer used here. If
  you changed it in `menuconfig`, keep it at 16-bit or convert `bits_per_pixel` accordingly.
- Buzzer/speaker/backlight calls are simple I2C writes and don't need to be
  cleared — the extender applies them immediately.
