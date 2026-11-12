# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- `GET /api/v1/wifi/scan`: nearby Wi-Fi networks (name, signal, secured or open), so the network used for updates can be
  picked from a list instead of typed by hand.

### Changed

- Web app redesigned: a small design system, a bottom tab bar with four tabs (Devices, Update, Settings, System),
  switches and fields with units instead of bare inputs, a save bar that appears only when settings changed, and layouts
  that no longer break on small screens. The input tester is opened from the Devices tab.

### Fixed

- Pairing: a trackball or mouse that also declares a keyboard collection in its report map (for shortcut keys) was
  given the keyboard code challenge, so it could not be paired. Devices with pointer axes now always get the click
  challenge.

## [1.0.0] - 2026-11-12

### Fixed

- WebSocket handshake now rejects cross-site pages (cross-site WebSocket hijacking) and the API requires the `Host` to be the
  dongle's own name (DNS rebinding); the captive-portal redirect matches host names exactly.
- HID normaliser: signed overflow when summing many 32-bit axis fields of one usage (found by fuzzing).

### Added

- USB composite HID device (TinyUSB): boot-protocol keyboard (interface 0) and mouse with AC Pan, consumer control
  and system control (interface 1); keyboard LED output report, suspend/resume detection, remote-wakeup plumbing.
- `hid_report` pure component: report descriptors and multi-source merge logic (union of keys/buttons, summed
  deltas with remainder handling), with host unit tests.
- Self-test now also checks the USB HID driver; `usb_state` in `/api/v1/status` and `/api/v1/diagnostics`.
- `debug_usb` build overlay: `POST /api/v1/debug/input` injects test input without BLE.
- `tools/check.sh`: local mirror of the CI pipeline.

- BLE central (NimBLE, up to 4 peripherals + 1 pairing candidate): HOGP client (report map, report references,
  notifications, battery, keyboard LEDs), persistent device registry, allow-list auto-reconnect, 7.5 ms interval
  request with measured values, per-device RSSI/latency/battery statistics.
- Pure host-tested components: `hid_parser` (generic report-map parser, garbage-run under ASan/UBSan),
  `hid_normalizer`, `combo` (both Shifts -> AP on, both Shifts + Esc 10 s -> Wi-Fi password reset), `lat_stats`.
- `bridge`: BLE -> USB forwarding, key release on disconnect, host LED sync, combo task.
- REST: `/devices`, `/devices/{id}` (+ `/report-map`), `/pairing/scan|start|cancel`; WebSocket `device.updated|removed`.
  M3 pairing is Just Works; the secure pairing flows come in M4.

- Complete web app (M5): setup wizard with forced password change and printable QR sticker (Wi-Fi + `hubble.local`),
  dashboard with device cards, pairing flow (scan, passkey, quarantine challenge, physical confirmation, legacy
  override, lock), device detail (rename, forget, stats, session battery history, report-map export), input tester,
  settings, backup/restore, factory reset, i18n EN/IT, skip link, mock API for every endpoint, Playwright coverage.
- REST: `GET/PUT /settings` now includes `ap_boot_window_s` and `ap_idle_timeout_s`; `GET /backup`, `POST /restore`
  (settings only, never bonds), `POST /system/factory-reset`; WebSocket `input.subscribe|unsubscribe` and `input.event`.
- Signed OTA images (M6): RSA-3072 (the only scheme the ESP32-S3 supports for apps) without hardware secure boot; the
  firmware verifies the signature of every installed image, release builds need the `HUBBLE_SIGNING_KEY` secret.
  `tools/gen_dev_key.sh`, `docs/key-management.md`, ADR 6 revised.
- "Check for updates" (M6): saved Wi-Fi network (write-only, APSTA on demand), stable/pre-release channel, GitHub
  Releases API + `manifest.json`, HTTPS download with SHA-256 check, board check, minimum version, anti-downgrade
  confirmation, physical confirmation, `ota.state` event, `POST /ota/check|install`, `PUT /settings/sta`.
  Pure `ota_manifest` component with host tests.
- Power management (M7): follows the USB host. After a debounce the dongle enters low power (40 ms connection interval,
  peripheral latency 10, AP and LED off); resume restores 7.5 ms at once; remote wakeup setting (default on, the waking
  event is not forwarded). Status LED patterns (booting, AP, pairing, quarantine, update, no device) with brightness and
  optional "device connected" indication; configurable Shift-combo hold time. Pure `power` and `led_pattern` logic with
  host tests; `ap_policy` honours the host suspend; `docs/power.md`.
- Hardening (M8): fuzz targets (HID parser, manifest parser, DNS responder; libFuzzer in CI, `tools/fuzz.sh` locally), all
  host tests under ASan/LSan/UBSan, security review (`docs/security-review.md`), user guide with screenshots generated from the
  mock (`npm run screenshots`), web installer page (ESP Web Tools, images served from the docs site), stability test plan,
  latency method, key management and release checklist.
- Secure pairing (M4): Passkey Entry (DisplayOnly) for keyboards, quarantine + click/code challenges for Just Works
  devices, legacy pairing refused unless overridden, optional physical confirmation (both Shifts 3 s on a paired
  keyboard), 60 s lock after 3 failures, security badges (green/yellow/red). Pure `pairing` state machine with host tests.
- `GET/PUT /api/v1/settings` (`require_confirmation`), pairing status in `GET /api/v1/pairing/scan`, `pairing.*` events.

### Changed

- The USB Serial/JTAG console is removed (TinyUSB owns the USB PHY); see ADR 10. Logs stay in the web app.
- "Reboot to USB download mode" releases the USB PHY before resetting.

## [0.2.0] - 2026-11-11

Milestone M1: Wi-Fi access point, web server and OTA foundations (hardware gate: ESP32-S3-Zero).

### Added

- Wi-Fi SoftAP `HubBLE-XXXX` (WPA2, default setup password) with boot window, idle timeout and WebSocket heartbeat policy.
- Captive portal (catch-all DNS, DHCP option 114, probe-URL redirects) and mDNS `hubble.local`.
- HTTP server serving the embedded gzipped web app; REST API (`status`, `ota/status`, `ota/upload`, `diagnostics`,
  `logs`, `settings/wifi-password`, `system/reboot`, `system/reboot-download`) with CSRF guard; WebSocket `/api/v1/ws`
  (`system.status`, `log.line`, `ota.progress`).
- OTA upload streamed to flash with chip/checksum/board-marker checks, rollback self-test (60 s), boot-failure counter.
- Reboot to USB download mode, RAM log buffer, diagnostics.
- Web app: Update and System pages, live WebSocket client, Wi-Fi password form, mock REST + WebSocket, i18n additions.
- Test overlays `rollback_test` and `ap_timeout_test`; docs (OpenAPI, WebSocket, OTA and recovery, M1 checklist), ADRs 8-9.

### Changed

- Serial console moved to the native USB Serial/JTAG port (the S3-Zero has no UART bridge).
- Faster release pipeline: no CI re-run on tags (verifies the green CI of the tagged commit instead), one parallel firmware build per board, separate publish job.
- Docs site is deployed from `main` by `docs.yml` instead of from the release tag.

## [0.1.1] - 2026-11-11

### Fixed

- Release workflow: publish the GitHub release from a plain runner (the ESP-IDF container has no `gh`).
  The `v0.1.0` tag exists but never produced a release; `v0.1.1` is the first published M0 pre-release.

## [0.1.0] - 2026-11-11

First pre-release: project bootstrap. No functional features yet.

### Added

- Repository layout, license (MIT), contributing/security docs, templates, Dependabot.
- ESP-IDF v5.5.6 firmware skeleton for ESP32-S3-Zero (4 MB) and XIAO ESP32-S3 (8 MB):
  Kconfig board selection, `board` and `led` components, boot banner with version,
  dual-OTA partition tables with rollback enabled, `semver` and `web_assets` components.
- Host unit-test harness (Unity + CMake).
- Web app skeleton (Vite, Preact, strict TypeScript) with mock API, EN/IT i18n stub,
  Vitest, Playwright smoke tests and a gzip size check; embedded into the firmware image.
- Documentation site (MkDocs Material), ADRs, Doxygen and TypeDoc configuration.
- GitHub Actions CI and tag-triggered release pipeline (image signing stubbed until M6).
