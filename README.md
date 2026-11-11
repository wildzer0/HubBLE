# HubBLE

A tiny ESP32-S3 USB dongle that connects to up to four Bluetooth Low Energy
keyboards and mice at once and presents them to the host PC as a single,
standard, wired USB HID device. The host needs no Bluetooth, no drivers and no
software. A built-in web app (served over the dongle's own Wi-Fi access point)
handles pairing, settings, diagnostics and over-the-air updates.

> **Status:** pre-release, milestone M1 (Wi-Fi AP, web app and OTA updates). BLE and
> USB HID forwarding are not implemented yet. See [CHANGELOG.md](CHANGELOG.md) and the roadmap in
> [CLAUDE.md](CLAUDE.md).

## Supported boards

| Board | Flash | Notes |
|---|---|---|
| ESP32-S3-Zero style (ESP32-S3FH4R2) | 4 MB | primary target |
| Seeed Studio XIAO ESP32-S3 | 8 MB | secondary target |

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
