# HubBLE — Agent progress

> Resume protocol: in any new session, read `CLAUDE.md` first, then this file, and continue from "Next".

## Current milestone: M0 — Project bootstrap (target `v0.1.0`)

### Decisions (confirmed by the user)
- Repo: `wildzer0/HubBLE`, **public**, license **MIT**.
- ESP-IDF pinned to **v5.5.6** (latest stable 5.x, verified online 2026-11-11).

### Done
- Read CLAUDE.md in full.
- Environment check: git 2.53, gh 2.46 (auth `wildzer0`), node 22.23, npm 10.9, python 3.14.4,
  cmake, ninja, gcc, clang-format present. Docker and ESP-IDF missing.
- Git repo initialised; public repo `wildzer0/HubBLE` created, `main` pushed, branch `milestone/m0-bootstrap` created.
- 5.1 done (hygiene files).
- 5.4, 5.5, 5.6, 5.7 done locally (webapp, embed, docs, CI workflows) — pending: CI run on GitHub, release.
- 5.3 done: host Unity tests (semver, web_bundle) pass: `cmake -S firmware/test -B firmware/test/build && cmake --build firmware/test/build && ctest --test-dir firmware/test/build`.
- 5.2 done: firmware builds for BOTH boards locally (s3_zero app 0x3a4b0 in 1.875 MB slot; xiao_s3 in 3 MB slot).
  Components: board, led, semver (pure, for host tests), web_assets (bundle reader + embed). Rollback enabled.
  Build: `. ~/esp/esp-idf/export.sh; tools/build-firmware.sh [s3_zero|xiao_s3|all]` (needs webapp/dist).

### Current milestone: M1 — Wi-Fi AP, web server, OTA foundations (target v0.2.0, ⛔ hardware gate, board: S3-Zero)
Branch `milestone/m1-wifi-web-ota`. Plan / sub-steps:
 1. Pure logic + host tests: log_buffer, ap_policy, dns reply builder, settings model (password rules), http_guard (CSRF), board_marker scanner, boot_guard.
 2. IDF components: settings(NVS), wifi_ap, captive_portal(+mDNS), web_server(+ws), api, ota(+selftest), diag. Console -> USB Serial/JTAG (M0 forgot; S3-Zero has no UART chip).
 3. Web app: Update + System pages, ws client, mock REST+WS, i18n, e2e.
 4. Docs (openapi, websocket, ota-and-recovery, testing/m1.md), CHANGELOG, PR, release v0.2.0.
 5. Gate: build test images v0.2.1-rc.1 (good) and a forced-self-test-failure build; user flashes factory.bin once, then OTA + rollback.
Design notes: board check = marker string `HUBBLE_BOARD_ID=<id>;` embedded in image and scanned while streaming; AP stays on while no bonded devices (BLE not yet present => always on unless Kconfig HUBBLE_ASSUME_BONDED).

### Environment notes
- ESP-IDF v5.5.6 installed locally at `~/esp/esp-idf` (user-approved, home only); tools in `~/.espressif`.
  Python 3.14 works. Activate with `. ~/esp/esp-idf/export.sh`.

### Open issues
- none
