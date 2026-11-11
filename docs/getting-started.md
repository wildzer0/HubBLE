# Getting started

## Supported boards

| Board id | Board | Flash |
|---|---|---|
| `s3_zero` | ESP32-S3-Zero style (ESP32-S3FH4R2) | 4 MB |
| `xiao_s3` | Seeed Studio XIAO ESP32-S3 | 8 MB |

!!! warning
    The chip must be an **ESP32-S3**. Some listings mention an "ESP32-C3-Zero";
    it has no USB OTG and will not work.

## First flash (USB, once)

After the first flash, every update is done over the air (from M1 on).

1. Download `hubble-<board>-vX.Y.Z-factory.bin` from the
   [latest release](https://github.com/wildzer0/HubBLE/releases).
2. Put the board into download mode: hold **BOOT**, plug in the USB-C cable,
   release BOOT (on the S3-Zero there is no USB-UART chip, so this is required).
3. Flash the merged image at offset `0x0`:

    ```bash
    pip install esptool
    esptool.py --chip esp32s3 write_flash 0x0 hubble-s3_zero-vX.Y.Z-factory.bin
    ```

4. Unplug and replug. The LED flashes white briefly; the serial log (USB
   Serial/JTAG, e.g. `/dev/ttyACM0`) prints the boot banner with the firmware version.

A browser-based installer (ESP Web Tools) will be added to this site in a later
milestone.

## First use (from v0.2.0)

1. Join the Wi-Fi network `HubBLE-XXXX` (XXXX = last four hex digits of the board's MAC) with the
   default setup password `hubble-setup`.
2. Your phone should open the web app by itself (captive portal). Otherwise browse to
   `http://hubble.local` or `http://192.168.4.1`.
3. Open **System** and change the Wi-Fi password straight away (at least 10 characters).
4. Later updates: **Update** → choose the `*-ota.bin` file. See [OTA and recovery](ota-and-recovery.md).

## Building from source

See [Development](development.md).
