# OTA updates and recovery

## How an update works (M1: file upload)

1. Open **Update** in the web app, pick a `hubble-<board>-vX.Y.Z-ota.bin` file and press **Install update**.
2. The dongle streams the file into the inactive OTA slot. HID forwarding is not touched (there is none yet) and
   the Wi-Fi access point is held on until the upload ends.
3. Checks while the file arrives and when it ends:
    - first bytes: ESP application image for the ESP32-S3;
    - ESP-IDF checksum and SHA-256 validation of the whole image;
    - **board check:** every image embeds `HUBBLE_BOARD_ID=<id>;`; an image for another board is refused;
    - project name `hubble`.
4. The dongle selects the new slot and reboots. The page waits for it and shows the new version.

Not yet implemented (M6): signature verification, anti-downgrade, "check GitHub for updates". Until then any image
that passes the checks above is accepted, so only install files you trust.

## Rollback and self-test

A freshly installed image boots in the bootloader's *pending verify* state. It must pass its **self-test**
within 60 s (`CONFIG_HUBBLE_SELFTEST_TIMEOUT_S`): settings readable, Wi-Fi AP initialised, web server up
(USB HID and BLE are added in M2/M3). Then it is confirmed and becomes the permanent image.

If the self-test does not pass in time, the image is marked invalid and the dongle reboots into the previous
image. The **Update** page then shows the failed attempt and the LED blinks red three times at boot.
While an image is still pending, further updates are refused (HTTP 409).

A **boot-failure counter** (NVS) is incremented on every boot and cleared when the self-test passes. After 3
boots in a row without a passed self-test the running image is rolled back as well (this covers crash loops).

## Reboot to USB download mode

**System → Reboot to USB download mode** restarts the chip into the ROM bootloader so you can re-flash over USB
without pressing BOOT. Unplug and replug the dongle to leave this mode. (When TinyUSB takes over the USB port in
M2 this will be re-verified.)

## Recovery paths

| Situation | What to do |
|---|---|
| Bad update | Automatic rollback (see above). |
| Cannot reach the web app | Join `HubBLE-XXXX`, open `http://192.168.4.1`. The AP is always on while no BLE device is bonded. |
| Forgotten Wi-Fi password | Not yet possible without USB (the Shift-combo reset needs BLE, M3). Re-flash the factory image over USB: it keeps your NVS but the password is stored there, so also erase flash: `esptool.py erase_flash`. |
| Dongle not responding | Hold BOOT while plugging in, then flash `*-factory.bin` at `0x0` (see [Getting started](getting-started.md)). |
