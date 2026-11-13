# Changes: demo project -> BLED display unit

## UART link bring-up (display <-> measurement unit)
* `bled_link.c`: UART1 is set up in its own task pinned to core 1, 500 ms after start-up; pins routed explicitly (TX IO20, RX IO19), pull-up on RX, no use of the driver before it is ready.
* `app_main.c`: the LVGL task is created right after `ui_app_create()` (before WiFi), and starts drawing only when start-up is finished.  Cause of the earlier black screen: after WiFi had taken most of the internal RAM there was no free 16 KB block left for the LVGL task, and its creation failed silently.
* `wifi_manager.c`: 8 static / 16 dynamic RX buffers (was 16 / 32) to leave internal RAM free; `esp_wifi_init` failure no longer aborts (it used to cause a reboot loop with a dark screen).
* `test/host/Makefile`: `test_core` is built with `-DBLED_LINK_SIMULATE=1` (it tests the built-in simulator, so it must not follow the default of `bled_config.h`).
* Tested on the board with an ESP32-S3 stand-in unit: PING from the display every second, STAT/BPRG/BDON/PAD/ERR from the unit, DIP S1/S0 = 0/1.


## Removed
* `ui_punchclock.c/.h`, `ui_air_quality.c/.h` (the two tabs you asked to delete)
* `door_poller.c/.h`, `sensor_poller.c/.h` - they only fed those tabs and the weather cards
* `esp_http_client` and `json` from `src/CMakeLists.txt` (nothing uses them any more)
* `ui_clock_weather.c/.h` -> replaced by `ui_clock.c/.h` (weather cards removed)

## Added (src/)
`bled_config.h`, `bled_proto`, `bled_link`, `bled_storage`, `bled_core`, `bled_time`,
`ui_theme`, `ui_statusbar`, `ui_newcase`, `ui_records`, `ui_clock`; docs in `docs/`.

## Modified
* `ui_app.c/.h` - status bar + 6 tabs, swipe between tabs disabled (tap the tab bar), drives the BLED model
* `ui_home.c` - Dashboard rebuilt with live system status; About page updated; palette. Profile / Brightness /
  Sound pages and the sidebar are as before (sidebar selection is now clearly highlighted)
* `ui_clock.c` - Set Time / Set Date code unchanged; the 3 weather cards became case cards
* `ui_calendar.c`, `ui_connections.c` - background colour; Calendar slightly shorter to fit under the status bar;
  Bluetooth placeholder text no longer mentions home automation
* Screen flicker fix: periodic refresh code no longer re-applies identical colours / borders / hidden flags (LVGL redraws an object every time, even for the same value). New helpers `ui_set_bg_color`, `ui_set_border`, `ui_set_hidden` in `ui_theme.c`; used by the Dashboard banner, New Case, Clock cards and status bar. The Clock tab now only rewrites a label whose text changed.
* `app_main.c` - removed the two pollers, added `bled_core_init()`, LVGL task stack 8 KB -> 16 KB (8 KB overflowed with the larger UI)
* `src/CMakeLists.txt` - source list; `fatfs` and `sdmmc` added to REQUIRES
* `sdkconfig.defaults` - **comments only**. No sdkconfig value was changed.

## First-run checklist
1. DIP switches S1/S0 = 1/0, FAT32 card inserted.
2. `pio run -t upload`. The first build is a full rebuild (the `.pio` cache is not part of this package).
3. Watch the log for `SD card mounted at /sdcard`. If it says `SD mount failed`, check card format and switches.
4. Dashboard should show "All systems ready" (amber **SIM** badge = simulated measurement unit).
5. New Case -> walk through the wizard -> Start case -> *Simulate pad* a few times -> End case -> Records tab.
6. Take the card to a PC: `BLED/C0000001.CSV` should contain the rows described in `bled_storage.h`.

## Unit link moved to UART0-OUT (IO43 TX / IO44 RX)
At DIP S1/S0 = 1/1 (the only setting that connects the SD slot on board v1.4) IO19/IO20 are shared with the
microphone and UART1 stops working. The link now uses UART0-OUT so SD and unit link work together at 1/1.
Display logs are switched off in real-link mode (UART0 is the console). Unplug the USB-serial cable while the unit is attached.
