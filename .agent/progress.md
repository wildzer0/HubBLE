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
Branch `milestone/m1-wifi-web-ota`. Status:
 - DONE locally: pure logic + host tests (8 suites), IDF components (settings, log_buffer, wifi_ap, captive_portal, web_server, api, ota, diag),
   web app (Update/System pages, ws client, mock REST+WS, 19 unit + 9 e2e tests), docs, CHANGELOG [0.2.0], test overlays.
 - Both boards build locally (s3_zero ~0xd4e30 bytes). NOT testable without hardware: Wi-Fi, OTA flow, rollback, heap.
 - Gate files staged in ~/hubble-m1-gate/: v0.2.1-rc.1 (good) and v0.2.1-rc.2 ROLLBACK-TEST (self-test forced to fail).
 - NEXT: push branch, PR, CI green, squash-merge, wait main CI, tag v0.2.0, verify release (tests the NEW fast release.yml for the first time),
   then final report in Italian + gate instructions (docs/testing/m1.md). Do NOT start M2 until the user confirms the gate.
Design notes: board check = marker `HUBBLE_BOARD_ID=<id>;` scanned while streaming; AP stays on while no bonded devices
(BLE not yet present => always on unless Kconfig HUBBLE_ASSUME_BONDED, overlay ap_timeout_test).

### Environment notes
- ESP-IDF v5.5.6 installed locally at `~/esp/esp-idf` (user-approved, home only); tools in `~/.espressif`.
  Python 3.14 works. Activate with `. ~/esp/esp-idf/export.sh`.

### Open issues
- none
