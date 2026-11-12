# Hardware

Facts verified against vendor documentation on 2026-11-11.

## ESP32-S3-Zero style board (`s3_zero`)

- SoC ESP32-S3FH4R2: 4 MB in-package flash, 2 MB quad PSRAM. Check the chip marking: it must read `ESP32-S3`.
- On-board WS2812 RGB LED on **GPIO21** — confirmed by the
  [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Zero).
- **BOOT = GPIO0.** No USB-UART chip: native USB only; hold BOOT while plugging in for download mode.
- Waveshare notes GPIO33–GPIO37 are not broken out because they are reserved for octal PSRAM on some
  variants; with the quad-PSRAM FH4R2 this does not constrain HubBLE (it does not use those pins).
- The `ceramic antenna` is on-board; range is limited. Not yet measured by the project.

## Seeed Studio XIAO ESP32-S3 (`xiao_s3`)

- Single user LED on **GPIO21, active-low** (LOW = on) and BOOT on GPIO0, per the
  [Seeed wiki](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started).
- 8 MB flash / 8 MB octal PSRAM and the U.FL antenna connector: **not confirmed** by the sources
  consulted in M0 (vendor spec table not reached). To verify on hardware or from the Seeed spec page.

All GPIO numbers live in `firmware/components/board/Kconfig`; no other file may hard-code them.

## Console

From M2 the native USB port belongs to TinyUSB (no serial console, ADR 10). Until M1 logs went to the USB Serial/JTAG port (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`): the S3-Zero has no USB-UART
bridge. In M2 TinyUSB takes over the USB PHY, so the console strategy (and release builds without any CDC
interface, `CLAUDE.md` §3.2) will be revisited then.
