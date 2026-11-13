# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.3.0-rc.5] - 2026-11-13

### Fixed

- "Check for updates": the dongle now makes its own lookups use the router's DNS server (with the access point and the
  station up at once they could be answered by the captive-portal responder, which maps every name to the dongle itself),
  and checks that api.github.com resolves to a real address before connecting.

### Added

- When a check fails, the update page shows a technical detail (the stage, the error code, the DNS answer, free internal memory
  and the largest free block), and the log carries the same lines, so a failure no longer ends in a generic "network error".

## [1.3.0-rc.4] - 2026-11-13

### Fixed

- While a configuration session over USB was open, the dongle kept the Wi-Fi AP on (the session counted as web-app activity),
  and with the AP on the Bluetooth devices stopped working properly. A USB session now keeps only the configuration window
  open; it never switches the Wi-Fi on.
- "Choose file" on the Update page did nothing on phones: the sign-in window of the Wi-Fi network cannot open the file chooser
  (the page now says so and points to the normal browser), and Android pickers greyed out every file because of the `.bin`
  filter (removed).

### Changed

- The USB configuration is **off by default** (setting `usb_link`, applied after a restart): with it off the dongle's USB
  descriptor is the original two-interface one. It is also started after the Bluetooth stack, so the radio gets its memory
  first. This is a safety measure after the first release candidates; turn it on in Settings to use the hosted app.

## [1.3.0-rc.3] - 2026-11-13

### Fixed

- "Check for updates" failed with "GitHub not reachable or download interrupted": the certificate bundle held only the common
  subset of root CAs, which lacks the USERTrust/Sectigo roots github.com and api.github.com use today, so every HTTPS request
  to GitHub failed the certificate check. The full root list is now embedded (+about 45 KB), and the log says why a request
  could not be opened.

## [1.3.0-rc.2] - 2026-11-13

### Fixed

- USB configuration link: the queue that carries the dongle's replies was never created, so the handshake never got an
  answer ("The dongle did not answer the handshake"). The page now also shows the technical reason of a failed connection,
  and the log records the link's activity (`OUT report from the host`, `HELLO from the host`).

## [1.3.0-rc.1] - 2026-11-13

### Added

- **Configuration over USB (WebHID).** The dongle exposes a vendor-defined HID interface (64-byte reports) and forwards the web
  app's requests to its own HTTP API over the loopback interface, plus the live events, so the app works with no Wi-Fi. The
  app is hosted on the project's HTTPS site (`/app/`); open it in Chrome or Edge and press *Connect the dongle*. The link
  answers only while the configuration window is open (the same window that switches the Wi-Fi on), so software on the computer
  cannot change the settings at will. Protocol in `docs/api/usb-link.md`; framing tested on both ends with the same vectors.
- The AP policy now exposes the configuration window (`config_open`), which an explicit "Wi-Fi off now" does not close, and a USB
  session counts as activity.
- Typing filter (off by default): ignore a key or mouse button pressed again within N ms of its release (bounce keys) and/or
  require a press to be held for N ms (slow keys). Applied to every paired keyboard and mouse before the USB report, effective
  at once. New settings `filter_enabled`, `filter_bounce_ms`, `filter_hold_ms`; pure `input_filter` component with host tests.
- Health check page (System → Run the check): USB, each device (connection, battery, latency, interval), memory, temperature
  and the last restart, with a copyable summary.
- Ways to switch the Wi-Fi on and to confirm a pairing or an update **without a keyboard**: a short press of the BOOT button
  on the dongle, or holding the left and right mouse buttons together for 3 s on a paired mouse or trackball. Until now a
  dongle without a connected keyboard could only be reached by replugging it.

### Fixed

- The web app's USB state values now match the firmware (`configured` instead of `active`).

## [1.2.2] - 2026-11-13

### Fixed

- Battery: the first Battery Level characteristic of the service is kept (the last one used to win when a device lists
  several), and the log shows what the device returns and which handles are used, to tell a wrong value reported by the
  device from a wrong reading.

## [1.2.1] - 2026-11-13

### Fixed

- The quiet captive-portal mode (answering the connectivity probes as if there were internet) was the default in 1.2.0, but
  on Android it makes the browser stay on mobile data and fail to reach `192.168.4.1` (`ERR_TOO_MANY_RETRIES`): the Wi-Fi
  cannot be "validated" because the HTTPS probe has no valid certificate. The pop-up is the default again; the quiet mode
  stays available in Settings, with the Android caveat documented.

## [1.2.0] - 2026-11-13

### Added

- Compatibility reports: **Prepare a report** on a device's page shows exactly what would be shared (report map, identifiers
  read from the device's Generic Access and Device Information services, connection values, latency) and lets you download
  or send it. `GET /api/v1/devices/{id}/report`, schema in `docs/api/device-report.schema.json`.
- `service/`: a Cloudflare Worker that collects the reports, validates them, de-duplicates and rate-limits, and publishes
  aggregate statistics; `docs/compatibility.md` and `tools/build_compat_page.py` turn them into a public table.
- Low-battery warning (20 % or less) on the Devices page, with a badge on the card.
- Setting *Open this page when joining the Wi-Fi*.

### Changed

- Joining the Wi-Fi no longer pops up the page by default after the setup: the dongle answers the operating systems'
  connectivity probes as if the network had internet, so the phone stays connected. The QR code of the sticker now opens
  `http://192.168.4.1`. The pop-up stays on during the first-run setup.
- Cooler: CPU at 80 MHz and the background reconnect scan at 15 % duty.

## [1.1.7] - 2026-11-12

### Fixed

- Pairing a second device could drop the first one: the discovery scan ran at 60 % duty and starved the connected
  device's 7.5 ms connection events. While another device is connected the scan now uses 20 % duty.
- The battery level was read once at connection and then only on notification, which many peripherals send rarely or
  never; it is now read again every 30 s and the log shows each change.
- The log now says why a device disconnected (HCI reason code).

## [1.1.6] - 2026-11-12

### Fixed

- A trackball whose report map also contains a keyboard collection with modifier keys was still counted as "an already
  paired keyboard", so pairing a real keyboard asked for a Ctrl + Alt confirmation that nobody could give. The parser now
  records the first top-level collection (what the device mainly is) and the classification uses it; the stored flag is
  re-evaluated at every connection, so an affected trackball is corrected without pairing it again.

## [1.1.5] - 2026-11-12

### Fixed

- Release workflow: with a single board the downloaded build artifact has no per-board folder, so publishing 1.1.4 failed.
  1.1.4 was therefore never published; 1.1.5 contains everything listed under 1.1.4.

## [1.1.4] - 2026-11-12

### Changed

- The keyboard gesture is now **Ctrl + Alt** (either side) held for 3 s instead of the two Shift keys, and
  **Ctrl + Alt + Esc** for 10 s for the password reset. It also confirms a pairing or an online update. The setting
  keeps its old key name `shift_hold_ms` so existing backups still restore.
- The XIAO ESP32-S3 target is paused: CI, releases and the web installer build only the S3-Zero (the XIAO sources stay in
  the tree). `docs/development.md` explains how to re-enable it or add another board.

### Added

- "Switch Wi-Fi off now" on the Devices page (`POST /api/v1/system/wifi-off`): the AP goes off within a second and stays
  off until the Ctrl + Alt gesture or a restart. Refused while no device is paired.

### Fixed

- The Wi-Fi AP policy never knew about bonded devices (the count was hard-wired to 0 since M1), so the AP never switched
  off by itself, kept the board hot and competed with BLE for the radio. It now follows the boot window and idle timeout
  as specified; with no bonded device it stays on.
- The "keyboard" flag stored with a bond is re-evaluated on every connection. Mice that older firmware had classified as
  keyboards made a new pairing wait for a physical confirmation from a keyboard that did not exist.
- The scheduler tick was 100 Hz, so the 1 ms retry of the USB report task rounded to zero and could spin; it is now
  1 kHz and the retry is at least one tick.
- While a device is connected, the background reconnect scan for the others is thinned out (15 % duty instead of 60 %)
  so it no longer steals radio time from the 7.5 ms connection and from the Wi-Fi AP.
- The NimBLE host task has a 6 KB stack (it was 4 KB) for the GATT callbacks and report hand-off.

## [1.1.3] - 2026-11-12

### Fixed

- **No HID input at all**: descriptor discovery was started one handle too late, so NimBLE dropped the first descriptor
  of every characteristic (the CCC or the Report Reference) and silently skipped characteristics with a single
  descriptor. Nothing was subscribed, so no key or click ever arrived and the pairing challenge could not be passed.
  The Battery Level CCC had the same problem.

### Added

- Pairing page: while a challenge is active it shows how many signals the dongle has received from the device being
  paired, to tell "the device is silent" from "the clicks were not recognised" (`rx_reports` in the pairing status).
- A banner on every other page while a pairing is in progress, with "Open" and "Cancel", so a pairing left behind
  cannot trap the dongle in its challenge.

## [1.1.2] - 2026-11-12

### Fixed

- Pairing: the challenge was chosen wrongly for devices that expose both a keyboard and a pointer (a keyboard with a
  mouse report got the click challenge in 1.1.0/1.1.1). The advertised GAP appearance now decides, then the report map
  (a pointer needs modifier keys as well to count as a keyboard).
- A peripheral that re-encrypts right after pairing could restart HID discovery in the middle of the first run; the
  second start is now ignored.
- Web app: icons carry their own size, so a stale cached stylesheet can no longer blow them up; the System page shows the
  log lines from before the page was opened.
- HID discovery now logs one line per characteristic (handle, report ID and type, CCC, subscribed or not) to find
  devices that report "ready" but send no input.

### Changed

- Wi-Fi transmit power is capped at 10 dBm by default (`CONFIG_HUBBLE_WIFI_TX_POWER_DBM`) to run cooler.

### Added

- Chip (die) temperature in the diagnostics API and on the System page.

## [1.1.1] - 2026-11-12

### Fixed

- Web app: the device list while pairing showed a giant icon that squeezed the name; icons are now sized.
- Pairing diagnostics: the log now says which attribute a quarantined device notifies on (handle and length only, never
  the content), to find why a trackball's clicks were not seen.

## [1.1.0] - 2026-11-12

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
