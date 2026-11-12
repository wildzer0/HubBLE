# Getting started

## Supported boards

| Board id | Board | Flash |
|---|---|---|
| `s3_zero` | ESP32-S3-Zero style (ESP32-S3FH4R2) | 4 MB |
| `xiao_s3` | Seeed Studio XIAO ESP32-S3 | 8 MB (support paused: not built or released) |

!!! warning
    The chip must be an **ESP32-S3**. Some listings mention an "ESP32-C3-Zero"; it has no USB OTG and will not work.

## First flash (USB, once)

The easiest way is the [web installer](install.md). By hand:

1. Download `hubble-<board>-vX.Y.Z-factory.bin` from the [latest release](https://github.com/wildzer0/HubBLE/releases).
2. Put the board into download mode: hold **BOOT**, plug in the USB-C cable, release BOOT.
3. Flash the merged image at offset `0x0`:

    ```bash
    pip install esptool
    esptool.py --chip esp32s3 write_flash 0x0 hubble-s3_zero-vX.Y.Z-factory.bin
    ```

4. Unplug and replug. The LED flashes white briefly. There is no serial console in release builds (the USB port belongs
   to the HID device); logs are shown on the **System** page of the web app.

## First use

1. Plug the dongle into the computer. Join the Wi-Fi network `HubBLE-XXXX` (XXXX = last four hex digits of the board's
   MAC) with the default setup password `hubble-setup`.
2. Your phone opens the web app by itself (captive portal). Otherwise browse to `http://hubble.local` or
   `http://192.168.4.1`.
3. The setup wizard asks for a new Wi-Fi password (at least 10 characters) and shows a printable QR sticker. Then pair
   your first keyboard or mouse. The full tour with screenshots is in the [User guide](user-guide.md).
4. Updates: **Update**, either *Check for updates* or choose a `*-ota.bin` file. See [OTA and recovery](ota-and-recovery.md).

## Building from source

See [Development](development.md).
