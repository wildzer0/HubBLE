# Architecture

The overall picture is in `CLAUDE.md` §3.1; this page lists what exists in the firmware today. The generated
[API reference](api/index.md) documents every function; each source file starts with a header comment (purpose,
responsibilities, dependencies, threading model).

```text
 BLE keyboards/mice ─► ble_central + hogp_client ─► hid_parser / hid_normalizer ─► bridge ─► usb_hid ─► host PC
                                │                                                    ▲  │
                          pairing (state machine)                       combo, input_filter │
                                                                                            ▼
 Phone ◄─ Wi-Fi AP ─► web_server + api ◄─► settings (NVS)      wifi_ap, power, led, ota, diag
```

## Components

| Component | Purpose | Host-testable |
|---|---|---|
| `board` | Per-board constants selected via Kconfig, board marker embedded in every image | – |
| `led` | WS2812 / GPIO status LED and its patterns | patterns |
| `semver` | SemVer parse/compare (anti-downgrade) | yes |
| `web_assets` | Embedded gzipped web app + bundle reader | reader only |
| `settings` | NVS settings (Wi-Fi password, AP timings, LED, filters, update channel, ...) | validation rules |
| `log_buffer` | RAM ring buffer fed by an `esp_log` hook (live logs, download) | ring buffer |
| `wifi_ap` | SoftAP `HubBLE-XXXX` manager and AP on/off policy, station mode for update checks | policy |
| `captive_portal` | DNS responder answering every query with the dongle IP, platform probe detection, mDNS `hubble.local` | DNS reply, probe classifier |
| `web_server` | HTTP server: embedded web app, WebSocket `/api/v1/ws`, portal redirects | – |
| `api` | REST handlers `/api/v1/*` and the CSRF guard | CSRF guard |
| `ota` | Streaming install, board check, rollback self-test, boot-failure guard | board marker, boot guard |
| `ota_manifest` | Release manifest parsing, tag search, install decision (newer / downgrade / minimum version / signed) | yes |
| `ota_online` | GitHub check and HTTPS install (APSTA, SHA-256, physical confirmation) | – |
| `power` | Suspend debounce, low-power BLE profile (follows the USB host) | yes |
| `diag` | Diagnostics snapshot, reboot to download mode | – |
| `hid_parser` | Generic HID report-descriptor parser (fuzz-tested) | yes |
| `hid_normalizer` | Decodes input reports into normalised key / mouse / consumer events | yes |
| `hid_report` | The USB HID descriptors and the merge of several sources into one report | yes |
| `hogp_client` | HID-over-GATT client: discovery, report map, subscriptions, battery, identification | – |
| `ble_central` | NimBLE central: scanning, bonding, allow-list reconnect, device registry, connection parameters | – |
| `pairing` | Pairing state machine, passkey / click / code challenges, failure lock | yes |
| `lat_stats` | Latency statistics (min / avg / p95 / max) per device | yes |
| `combo` | Hold-combination detection (Ctrl + Alt, Ctrl + Alt + Esc, mouse buttons) | yes |
| `input_filter` | Optional bounce-keys / slow-keys filter | yes |
| `bridge` | Glue between BLE events, the filters, the merge and the USB device; power mode handling; input tester tap | – |
| `usb_hid` | TinyUSB composite HID device, suspend / resume, remote wakeup, keyboard LEDs | – |
| `usb_link` | Optional USB configuration interface (WebHID): carries the REST API and events over USB | framing |

Pure-logic code never includes ESP-IDF headers so it runs in the Unity host tests under `firmware/test/`.

## Device limits

| Limit | Value | Where |
|---|---|---|
| Peripherals connected and paired at the same time | **4** | `BLE_CENTRAL_MAX_DEVICES` (`ble_central.h`), `REG_SLOTS` (`device_registry.h`) |
| NimBLE connections | 5 (the four devices + one pairing candidate) | `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` |
| NimBLE bond store | 8 entries | `CONFIG_BT_NIMBLE_MAX_BONDS` |

"Paired" and "connectable" are the same thing here: every paired device owns one of the four registry slots, and the
slot index is its source index in the USB merge. To pair a fifth device one of the four must be forgotten first.

The limit of four is a **design decision of the project brief** (`CLAUDE.md` §1.1 and §3.3), not a hardware limit; the
controller could carry more links. The reasons behind it:

- **Latency budget.** The goal is at most 15 ms added latency with a 7.5 ms connection interval. Every connection needs
  radio time in each interval, and Bluetooth shares the radio with Wi-Fi, so each extra device makes the worst case
  worse.
- **Real use.** Four covers a keyboard, a mouse or trackball and two more peripherals, which is what the dongle is for.
- **Fixed memory.** The forwarding path has no dynamic allocation after start-up: filters, merge state and registry are
  static arrays sized by the slot count.

No per-count latency measurements were taken to pick the number; raising it would need the budget re-measured
(see [Latency](latency.md)).

The bond store is larger than the registry on purpose: pairing creates the bond before the device is registered, and a
pairing that fails or is cancelled deletes it again, so a few spare entries keep the store from running out in between.

## Web app embedding

`tools/pack_webapp.py` gzips every file of `webapp/dist` into one blob
(`u16 path_len | u32 data_len | path | gzip data`). The blob is linked into the
firmware with `EMBED_FILES` and read through `web_bundle.h`. One OTA therefore
updates firmware and web app together.

## Threads

| Task | Owner | Notes |
|---|---|---|
| `httpd` | esp_http_server | REST, static files, WebSocket receive, OTA upload (8 KB stack) |
| `ws_push` | web_server | 1 Hz `system.status` and `log.line` events |
| `input_tap` | bridge | Delivers input-tester events to the WebSocket (best effort, never blocks forwarding) |
| NimBLE host | ble_central | All GAP / GATT callbacks; also the producer of forwarded reports |
| `usb_hid` | usb_hid | Sends the merged reports through TinyUSB |
| `usb_link` | usb_link | Configuration link over USB (only when enabled) |
| `in_filter` | bridge | Ticks the typing filter while an element waits for its hold time |
| `power` | bridge | Applies the BLE connection profile on suspend / resume |
| `combo` | bridge | Samples the hold combinations |
| `wifi_ap` | wifi_ap | 1 Hz AP policy; owns the radio state |
| `dns` | captive_portal | UDP port 53 |
| `led` | led | Pattern player |
| `ota_check`, `ota_install` | ota_online | Short-lived workers of the online update |
| `selftest` | ota | Exits after confirming (or rolling back) the image |

Cross-task state is limited to volatile flags, spinlock- or mutex-protected copies (documented where each is declared),
an atomic busy flag in `ota`, and a spinlock around the log ring. The forwarding path never waits for Wi-Fi or HTTP work.

## Request flow

A phone joins the AP; DHCP option 114 and the catch-all DNS point it at `192.168.4.1`. Requests whose `Host` is not
`192.168.4.1`/`hubble.local` (OS connectivity probes) get a `302` to the app; other `GET`s are served from the
embedded gzipped bundle; `/api/v1/*` goes to the REST handlers.
