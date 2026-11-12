# OTA updates and recovery

## How an update works (file upload)

1. Open **Update** in the web app, pick a `hubble-<board>-vX.Y.Z-ota.bin` file and press **Install update**.
2. The dongle streams the file into the inactive OTA slot. HID forwarding is not touched (there is none yet) and
   the Wi-Fi access point is held on until the upload ends.
3. Checks while the file arrives and when it ends:
    - first bytes: ESP application image for the ESP32-S3;
    - ESP-IDF checksum and SHA-256 validation of the whole image;
    - **board check:** every image embeds `HUBBLE_BOARD_ID=<id>;`; an image for another board is refused;
    - project name `hubble`.
4. The dongle selects the new slot and reboots. The page waits for it and shows the new version.

5. **Signature:** the image must carry a valid RSA-3072 signature from the release key (`docs/key-management.md`);
   an unsigned or wrongly signed file is rejected when the upload ends and never becomes bootable.

## Check for updates (GitHub)

1. **Settings:** save the Wi-Fi network (home Wi-Fi or a phone hotspot) the dongle may use. The credentials are stored in
   the dongle, never shown again and not included in backups. Optionally pick the **Stable** or **Pre-release** channel.
2. **Update, Check for updates:** the dongle keeps its own access point on, joins your network (APSTA), asks the GitHub
   Releases API for the newest release of the channel, reads its `manifest.json` and leaves your network again.
3. The page shows the version, size, whether it is signed, and a link to the release notes.
4. **Install:** if *Require physical confirmation* is on and a keyboard is paired, hold both Shifts for 3 s on it
   (30 s allowed). The dongle then downloads the image over HTTPS (certificate bundle), checks its SHA-256 against the
   manifest while writing it to the inactive slot, and only then lets the bootloader API verify the signature and
   select the slot. HID forwarding keeps working during the download.

Refusals, each with its own message: no Wi-Fi saved, cannot join, GitHub unreachable, no image for this board, release not
signed, running version below the release's `min_version` (update in steps), hash mismatch, signature rejected.
**Anti-downgrade:** an older release is offered but cannot be installed unless you tick the explicit confirmation.

Limits: joining your Wi-Fi makes the radio follow your router's channel, so the dongle's own access point can move and
a phone may drop for a few seconds (reconnect if the page stops responding). The physical confirmation applies to the
online install, not to the file upload (which already needs the Wi-Fi password).

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
