# HubBLE — Agent progress

> Resume protocol: in any new session, read `CLAUDE.md` first, then this file, and continue from "Next".

## POLICY (user, 2026-11-12): local commits only
No push/PR/tag until the last milestone (see CLAUDE.md §0.2). Verify with `tools/check.sh quick|full`.
M1 hardware gate is DEFERRED (hardware arrives 2026-11-12): user chose to continue development; run docs/testing/m1.md when the board arrives.
Active branch: `milestone/m2-usb-hid` (local only).

## M2 — USB composite HID (branch milestone/m2-usb-hid, local only)
DONE (committed locally, `tools/check.sh full` green): `hid_report` (descriptors + merge, 13 host tests), `usb_hid` (esp_tinyusb 3.0.0,
2 HID itfs, suspend/resume, LED output, remote wakeup), self-test check, usb_state in API, `debug_usb` overlay + /api/v1/debug/input,
ADR 10, docs/testing/m2.md, CHANGELOG [Unreleased]. Console removed (CONFIG_ESP_CONSOLE_NONE).
UNVERIFIED on hardware: enumeration, BIOS, wakeup, and above all "reboot to download mode" with TinyUSB (docs/testing/m2.md E).
Next: M3 (BLE central) — ask the user about the B68 custom firmware first. Tag v0.3.0 only at the end (policy).

## M3 — BLE central and forwarding (branch milestone/m2-usb-hid continues, local only)
DONE (builds for both boards, check.sh full green; NOTHING hardware-tested): hid_parser, hid_normalizer, combo, lat_stats (host tests),
hogp_client, ble_central (+device_registry), bridge, REST /devices + /pairing, WS device events, ADR 11, docs/testing/m3.md.
B68 note: its BLE is the BK3632 chip (Oblio only sends UART frames); pairing slot = hold B/N/M 3 s; BLE name OBLIO. EM06 is stock.
Risks to check on hardware: allow-list connect needs public/static addresses; heap with NimBLE+Wi-Fi (target >=60 KB; PSRAM on S3-Zero is enabled
but NimBLE uses internal RAM); 7.5 ms interval acceptance; legacy pairing of the BK3632 (may be 🔴/🟡 in M4).
Fixtures pending: firmware/test/fixtures/{yunzii-b68,protoarc-em06}.hex.
Next: M4 (secure pairing: passkey, quarantine/challenges, state machine host-tested). Tag only at the very end.

## M4 — Secure pairing (same local branch)
DONE (builds, check.sh green; hardware-untested): pure `pairing` FSM (21 host tests), ble_central refactored onto it (DisplayOnly passkey,
quarantine routed through bridge, legacy check via ble_store_read_peer_sec().sc, confirmation via both-Shifts combo, 60 s lock),
security badges persisted in the registry (reg_rec_t gained sec/kbd: older registries reset), GET/PUT /settings require_confirmation,
pairing.* WS events, docs/pairing-and-security.md, security.md, docs/testing/m4.md.
Risks: BK3632 pairing mode unknown (passkey vs Just Works vs legacy); NimBLE must accept mitm=1 with DisplayOnly against NoInputNoOutput peers.
Next: M5 complete web app (Preact pages, setup wizard, QR, input tester, settings, backup/restore, i18n, Playwright). Tag only at the end.

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
 - DONE: PR #7 merged, v0.2.0 released (pre-release, 6 assets; new fast release.yml worked first time, ~8 min tag->release).
 - WAITING: user hardware gate (docs/testing/m1.md, files in ~/hubble-m1-gate/). Do NOT start M2 until the user confirms the gate.
Design notes: board check = marker `HUBBLE_BOARD_ID=<id>;` scanned while streaming; AP stays on while no bonded devices
(BLE not yet present => always on unless Kconfig HUBBLE_ASSUME_BONDED, overlay ap_timeout_test).

### Environment notes
- ESP-IDF v5.5.6 installed locally at `~/esp/esp-idf` (user-approved, home only); tools in `~/.espressif`.
  Python 3.14 works. Activate with `. ~/esp/esp-idf/export.sh`.

### Open issues
- none

## M5 — Complete web app (same local branch)

Done: all §3.7 pages except the Update-from-GitHub and the STA/LED/wakeup/combo-time settings (they depend on M6/M7 firmware
support and are added with those milestones); mock API for every endpoint; firmware: extended settings, backup/restore,
factory reset, input tester tap (`bridge_set_input_tap`, WS subscription with 15 s expiry). Web bundle ~29 KB gzipped.
Next: M6 (OTA from GitHub + signing; ask before generating the key). Tag only at the end.

## M6 — OTA from GitHub + signed images (same local branch)

Decision (user, 2026-11-12): RSA-3072 (S3 has no ECDSA for apps) + local DEVELOPMENT key only (git-ignored, firmware/keys/).
The RELEASE key must be generated with the user BEFORE the first push (M8), offline backup + secret HUBBLE_SIGNING_KEY.
Done: ota_manifest (host-tested), ota_online engine, wifi_ap STA, settings STA/channel, API (/ota/check, /ota/install, /settings/sta,
ota.state), web Update/Settings UI + mock + e2e, release.yml signing step, docs (key-management, ADR 6, security, ota).
Not verifiable without hardware: signature rejection on device, TLS to GitHub, APSTA channel behaviour, heap (docs/testing/m6.md).
Next: M7 (power management). Tag only at the end.

## M7 — Power management (same local branch)

Done: power_policy (debounce + low profile, host-tested), ap_policy host_suspended, led_pattern + led_status task,
settings (remote_wakeup, led_brightness, status_led, shift_hold_ms), ble_central_set_conn_profile, bridge power task,
usb_hid state callback, diagnostics power, web settings/System, docs/power.md, docs/testing/m7.md.
Not verifiable without hardware: wake-up behaviour, peripheral acceptance of low-power params, measurements.
Next: M8 hardening and 1.0 (needs the release key generated with the user, first push after asking). Tag only there.
