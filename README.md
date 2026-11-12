# HubBLE

A tiny ESP32-S3 USB dongle that connects to up to four Bluetooth Low Energy
keyboards and mice at once and presents them to the host PC as a single,
standard, wired USB HID device. The host needs no Bluetooth, no drivers and no
software. A built-in web app (served over the dongle's own Wi-Fi access point)
handles pairing, settings, diagnostics and over-the-air updates.

## Features

- Up to **4 BLE keyboards, mice and trackballs at once**, merged into one USB composite HID device (boot keyboard,
  mouse with wheel and pan, media and system keys). Works in BIOS/UEFI. Keyboard LEDs follow the host.
- **Secure pairing from the web app:** passkey entry, quarantine challenges for Just Works devices, legacy pairing refused
  by default, optional physical confirmation, rate limiting.
- **Web app** on the dongle's own Wi-Fi: setup wizard with QR sticker, dashboard, device details, input tester,
  settings, backup/restore, updates, diagnostics, live logs. English and Italian.
- **Over-the-air updates** with automatic rollback, from a file or from GitHub, signed images (RSA-3072), SHA-256,
  board and downgrade checks.
- **Follows the host:** low-power Bluetooth parameters and Wi-Fi/LED off while the computer sleeps, remote wakeup.

> **Status:** feature complete for 1.0. Hardware validation checklists are in `docs/testing/`. See
> [CHANGELOG.md](CHANGELOG.md).

## Supported boards

| Board | Flash | Notes |
|---|---|---|
| ESP32-S3-Zero style (ESP32-S3FH4R2) | 4 MB | primary target |
| Seeed Studio XIAO ESP32-S3 | 8 MB | paused: source kept, not built or released |

## Repository layout

- `firmware/` — ESP-IDF project (C17)
- `webapp/` — Vite + Preact + TypeScript web app
- `docs/` — documentation site (MkDocs Material)
- `tools/` — build and release helper scripts

## Getting started

See [docs/getting-started.md](docs/getting-started.md) and
[docs/development.md](docs/development.md).

## License

[MIT](LICENSE)
