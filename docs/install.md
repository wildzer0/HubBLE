# Web installer

Flash the dongle from the browser, once. Afterwards every update is done over the air from the web app.

Needs **Chrome or Edge** (Web Serial) on a computer, and a USB-C data cable. Put the board in download mode first: hold
**BOOT**, plug the cable in, release BOOT. (The boards have no USB-UART chip, so the browser talks to the chip's ROM
loader.)

<script type="module" src="https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module"></script>

## ESP32-S3-Zero style board (`s3_zero`, 4 MB)

<esp-web-install-button manifest="../firmware/manifest-s3_zero.json"></esp-web-install-button>

## Seeed Studio XIAO ESP32-S3 (`xiao_s3`, 8 MB)

<esp-web-install-button manifest="../firmware/manifest-xiao_s3.json"></esp-web-install-button>

!!! note
    The buttons appear once a stable release has been published; the images are the `*-factory.bin` files of the latest
    release. If the browser cannot flash, use `esptool` as described in [Getting started](getting-started.md).

After flashing, unplug and replug the dongle and continue with the first-run steps in [Getting started](getting-started.md).
