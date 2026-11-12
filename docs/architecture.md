# Architecture

See the diagram and component list in `CLAUDE.md` §3.1. In M0 only these
components exist:

| Component | Purpose | Host-testable |
|---|---|---|
| `board` | Per-board constants selected via Kconfig | – |
| `led` | WS2812 / GPIO status LED | – |
| `semver` | SemVer parse/compare (future anti-downgrade) | yes |
| `web_assets` | Embedded gzipped web app + bundle reader | reader only |
| `settings` | NVS settings (Wi-Fi password, AP timings, boot-failure counter) | validation rules |
| `log_buffer` | RAM ring buffer fed by an `esp_log` hook (live logs, download) | ring buffer |
| `wifi_ap` | SoftAP `HubBLE-XXXX` manager and AP on/off policy | policy |
| `captive_portal` | DNS responder answering every query with the dongle IP, plus mDNS `hubble.local` | DNS reply builder |
| `web_server` | HTTP server: embedded web app, WebSocket `/api/v1/ws`, portal redirects | – |
| `api` | REST handlers `/api/v1/*` and the CSRF guard | CSRF guard |
| `ota` | Streaming install, board check, rollback self-test, boot-failure guard | board marker, boot guard |
| `ota_manifest` | Release manifest parsing, tag search, install decision (newer / downgrade / minimum version / signed) | yes |
| `ota_online` | GitHub check and HTTPS install (APSTA, SHA-256, physical confirmation) | – |
| `diag` | Diagnostics snapshot, reboot to download mode | – |

Pure-logic code never includes ESP-IDF headers so it runs in the Unity host
tests under `firmware/test/`.

## Web app embedding

`tools/pack_webapp.py` gzips every file of `webapp/dist` into one blob
(`u16 path_len | u32 data_len | path | gzip data`). The blob is linked into the
firmware with `EMBED_FILES` and read through `web_bundle.h`. One OTA therefore
updates firmware and web app together.

## Threads (M1)

| Task | Owner | Notes |
|---|---|---|
| `httpd` | esp_http_server | REST, static files, WebSocket receive, OTA upload (8 KB stack) |
| `ws_push` | web_server | 1 Hz `system.status` and `log.line` events |
| `wifi_ap` | wifi_ap | 1 Hz AP policy; owns the radio state |
| `dns` | captive_portal | UDP port 53 |
| `selftest` | ota | exits after confirming (or rolling back) the image |

Cross-task state is limited to volatile flags and a spinlock-protected status copy in `wifi_ap`, an atomic busy flag
in `ota`, and a spinlock around the log ring.

## Request flow

A phone joins the AP; DHCP option 114 and the catch-all DNS point it at `192.168.4.1`. Requests whose `Host` is not
`192.168.4.1`/`hubble.local` (OS connectivity probes) get a `302` to the app; other `GET`s are served from the
embedded gzipped bundle; `/api/v1/*` goes to the REST handlers.
