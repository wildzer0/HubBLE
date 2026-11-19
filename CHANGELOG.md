# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.6.0] - 2026-11-19

### Changed

- **Phone touchpad redesigned.** The pointer accelerates with the finger (a flick crosses a large screen, a slow
  movement stays precise) and the speed has five levels; before, the speed at its maximum was only just usable. New:
  tap-and-drag for drag and drop (a second quick tap is a double click), a scroll strip, a middle button, vibration on a
  click, natural scrolling and a Mac switch (⌘ shortcuts) in the settings. The keyboard has sticky modifiers (tap for the
  next key, tap again to keep on) that also apply to typed letters, keys that repeat while held, an inverted-T of arrows
  and shortcuts (copy, paste, cut, undo, select all, switch window). `remote.text` accepts a modifier byte `m`.

## [1.5.1] - 2026-11-19

Same content as 1.5.0, which was tagged but never published: its CI failed on an end-to-end test that matched the "Unlock
input" button when looking for "Lock input" (a test bug, not a product one). Only that test changed.

## [1.5.0] - 2026-11-19

Not published (see 1.5.1). New features (input lock, phone touchpad), diagnostics for the problems still open, and a fix for devices in slots 4 to 7.

### Added

- **Phone touchpad.** The phone acts as touchpad and keyboard (pointer, tap-click, two-finger scroll and right click,
  held buttons for dragging, a text field, modifier and special keys). Off by default: the Devices page opens a
  5-minute window (`POST /remote/enable`, `remote_s` in `GET /status`), a restart closes it, it is refused while the
  input is locked and only WebSocket clients may use it. A held button or key is released if the phone goes quiet.
- **Input lock.** Esc + Backspace held for 2 s on a keyboard, or the Devices page, stops all input reaching the host
  (the peripherals stay connected); the same gesture or the page unlocks. Solid red LED while locked. The two keys are
  kept away from the host while held, and the state is not kept over a restart. New `POST /system/lock` and
  `/system/unlock`, and `locked` in `GET /status`.
- The log now survives a software reset (crash, watchdog, restart): the new run continues after the old text, with a
  `=== boot: reset reason ... ===` marker. A power-on or brown-out reset starts a fresh log (RAM is undefined then),
  and the marker still names the reason.
- A failed pairing reports the step it failed in (`failed_in` in the pairing status, shown on the page), and every
  change of the pairing state is logged. The connection parameters that a peripheral asks for are logged too (they
  are still accepted as asked).

### Changed

- The battery on the device page is a battery icon with the level and a colour (green, amber up to 30 %, red up to
  20 %) instead of a line chart without axes that only filled while the page was open; the dashboard cards use it too.
- The amber "no device" pulse no longer turns red at its dimmest point: it goes dark instead, and dim colours are
  rounded instead of truncated.

### Known issues

- After the computer sleeps, peripherals may not reconnect, and a pairing challenge sometimes does not appear. Both are
  under investigation: this release adds the log that survives a restart and the step in which a pairing failed.

### Fixed

- Devices in slots 5 to 7 (and slot 4, which shared the debug source) could not type or move: the USB merge had only
  five sources for eight device slots. It now has one per slot plus the debug and phone sources.
- The pairing page showed the result of the previous pairing (for example "paired") when it was opened again.

## [1.4.2] - 2026-11-18

### Added

- The reason of the last disconnect of each device (`last_disconnect` in `GET /devices`, shown on the Device detail
  page), to diagnose peripherals that drop the link.

## [1.4.1] - 2026-11-18

### Fixed

- Keyboards that query the central right after connecting (seen with the NuPhy Air60 V2: a Read By Type request on the
  ATT channel) were dropped after 30 s, because the ATT server was compiled out and the request went unanswered. The
  NimBLE peripheral role is now enabled so that the ATT/GATT server answers; no services or advertising are used.

## [1.4.0] - 2026-11-15

First stable release of the 1.4 line; it rolls up 1.3.1-rc.1 to rc.4 and 1.4.0-rc.1 to rc.3 (see those sections below).

### Added

- Up to **8 devices can be paired**, of which at most **4 are connected and forwarded at the same time**; the others wait
  offline and connect when a place frees up (`GET /devices` reports `max` and `max_links`).
- **Download crash dump** on the System page (`GET /api/v1/coredump`), and a boot-log line when a dump is stored.
- Diagnostics in the BLE log: interval, latency and timeout of every link plus the internal heap when a connect is
  refused, on every connection and after every disconnect.

### Changed

- From three registered devices on, all links run at 15 ms instead of 7.5 ms and the existing links are slowed down
  before a new one is created: the controller cannot fit a third link next to two at 7.5 ms. With one or two devices
  the 7.5 ms target is unchanged.
- The per-device HOGP state lives in PSRAM; the static device table in internal RAM drops from 13 KB to 2.5 KB.

### Fixed

- Pairing or reconnecting a third device failed while two were connected, and could drop the others.
- A peripheral that was switched off and on again did not reconnect until the dongle was replugged (the background
  connect kept a stale allow-list).
- Forgetting a device over the USB link failed with "Unexpected Content-Type".
- The device registry is read whatever number of slots it was written with, so a rollback or an update no longer
  forgets the paired devices.
- A pairing that the controller refuses is reported as `controller_busy` instead of a security failure and does not
  count towards the pairing lock; the keyboard and click challenge no longer loses a first character after a mistake.
- The BLE log buffer is no longer flooded by repeated refused background connects (back-off of 1, 2, 5, 10 s).

### Known issues

- A Logitech MX Keys Mini that is switched off and on again while two other paired devices are connected may not
  reconnect (the other devices reconnect fine); it does when the other keyboard is disconnected first. Under
  investigation: the BLE log of a failed attempt is needed.

## [1.4.0-rc.3] - 2026-11-15

### Fixed

- A peripheral that was switched off and on again did not reconnect until the dongle was unplugged. The background
  connect listens only for the devices that were in its allow-list when it started; as long as another paired device
  stayed away (so the connect never ended), a device that had been connected at that moment and then dropped was never
  added. The pending connect is now restarted whenever the allow-list it needs differs from the one it has (a device
  dropped, a device connected, or every place taken), and the log says so (`allow-list changed`).

## [1.4.0-rc.2] - 2026-11-15

### Fixed

- v1.4.0-rc.1 failed to start on a real dongle and rolled back, and the older firmware then discarded the paired
  devices because it could not read the eight-slot registry (`ESP_ERR_NVS_INVALID_LENGTH`). The registry is now read
  whatever number of slots it was written with (fewer: the rest stays empty; more: the extra ones are ignored; a corrupt
  size gives an empty registry as before), with host tests. The paired devices of a dongle that rolled back come back
  as long as nothing was paired or forgotten in between.
- Internal RAM: the per-device HOGP state (3 KB each) is allocated once at start-up in PSRAM instead of being static
  for every slot. The static device table drops from 26 KB (rc.1) and 13 KB (rc.4) to 2.5 KB. The cause of the rc.1
  start-up failure is not confirmed (the leading suspect is the 16 KB of extra internal RAM that eight slots needed).

### Added

- `GET /api/v1/coredump` and a **Download crash dump** button on the System page: the crash dump that the last panic
  stored in flash (CLAUDE.md §3.11). The boot log says when one is stored.

## [1.4.0-rc.1] - 2026-11-15

### Fixed

- Forgetting a device over the USB link failed with "Unexpected Content-Type": the link sent DELETE requests without a
  Content-Type, which the API's CSRF guard requires on every state-changing call.

### Changed

- Up to **8 devices can be paired** (was 4) while at most **4 are connected and forwarded at the same time**. Paired
  devices beyond the fourth wait offline and connect when a place frees up; a device paired while four are connected
  completes the pairing and then stays offline. The registry written by older firmware (four slots) is migrated on
  load; the NimBLE bond store grows to 10 entries and the allow-list to 8. `GET /devices` reports `max` (8) and the new
  `max_links` (4).

## [1.3.1-rc.4] - 2026-11-15

### Fixed

- Pairing or reconnecting a third device failed while two others were connected, and the third link dropped the others
  after a while. The log showed the cause: the controller cannot fit a third link next to two at 7.5 ms (it refuses
  Create Connection with 0x0D and also the Connection Update back to 7.5 ms with 0x12), whereas links at 15 ms leave
  room. From three registered devices on every link now uses 15 ms, and the existing links are slowed down to it before a
  new connection is created. With one or two devices the 7.5 ms target is unchanged.
- A mistake in the keyboard or click challenge that equals its first character no longer swallows that character: the
  sequence now restarts at position 1 instead of 0.

### Changed

- Diagnostics: the BLE log prints the interval, latency and timeout of every link plus the internal heap when a
  connect is refused, on every connection and after every disconnect. A refused background connect backs off
  (1, 2, 5, 10 s) and is logged only the first time and then every tenth time, so it no longer floods the 16 KB log
  buffer.

## [1.3.1-rc.3] - 2026-11-15

### Fixed

- The controller still refused a third connection at every interval up to 60 ms, so the schedule was not the cause: the
  controller was built with ESP-IDF's default of 6 activities (`CONFIG_BT_CTRL_BLE_MAX_ACT`) while the project allows 5
  connections plus scan and initiator. It is now 10 (the maximum) and the boot log prints the effective limits.

## [1.3.1-rc.2] - 2026-11-15

### Fixed

- With two devices connected, the controller refused every further connection (pairing and background reconnect,
  `BLE_ERR_CONN_REJ_RESOURCES`, `links=2` in the log) although nothing else was scanning or connecting: it could not fit
  another 7.5 ms link into its schedule. A refused connect is now retried with a doubling interval (7.5, 15, 30, 60 ms)
  and the link is then asked to move to the configured profile; the log shows the interval used and the result of that
  update.

## [1.3.1-rc.1] - 2026-11-15

### Fixed

- Pairing a second device while a keyboard was already connected failed at once with "security exchange not completed":
  the BLE controller rejected LE Create Connection (`BLE_ERR_CONN_REJ_RESOURCES`, 525) and every failure was reported as
  a security failure. The firmware now waits until the scan has really stopped, retries the connect a few times, logs the
  radio state (links, scan, connect) when it is refused, and reports a distinct `controller_busy` reason that does not
  count towards the 3-failure pairing lock. A refused background reconnect is retried after 1 s.

### Changed

- Documentation only, no firmware behaviour change: every firmware function, macro, struct field and enum value now has a
  Doxygen block with `@param` / `@return`, and the TypeDoc reference covers the whole web app (`src/`), not just four
  files.
- `docs/api/openapi.yaml` now has request and response schemas for devices, pairing, settings, backup and diagnostics, and
  documents `GET /devices/{id}/report` and the WebSocket upgrade; it was not valid YAML before (unquoted colons).
- `docs/api/websocket.md` lists every event in its table; `docs/architecture.md` describes all components and tasks and
  explains the limit of four devices (5 NimBLE connections, 8 bonds).
- New API overview page in the documentation site.

### Added

- `tools/check_docs.py` (undocumented C symbols and missing `@param` / `@return`) and `tools/check_openapi.py` (OpenAPI
  matches the routes registered in the firmware); both run in `tools/check.sh` and in CI. TypeDoc fails on undocumented
  exports.

## [1.3.0] - 2026-11-13

First stable release of the 1.3 line; it rolls up 1.3.0-rc.1 to rc.6 (see those sections below).

### Added

- Configuration and firmware updates over USB (opt-in setting) from the hosted web app, using WebHID.
- Compatibility reports, typing filter (off by default), guided health check, keyboard-less gestures, Wi-Fi network
  scan, "Wi-Fi off now" button, low-battery warning.

### Changed

- The manual `.bin` upload is no longer offered in the copy of the app served by the dongle over Wi-Fi: phone browsers
  cannot open a file chooser there. Use the online update, or the hosted app over USB, which still offers it.

### Fixed

- Online update check (TLS buffers in PSRAM, station DNS, retry) and OTA over USB (serialised frames, longer timeouts).

## [1.3.0-rc.6] - 2026-11-13

### Fixed

- USB link (web app): frames are now sent one at a time. A heartbeat could be interleaved inside a firmware chunk and
  corrupt the stream, which broke OTA over USB. Acknowledgement timeout raised from 10 s to 30 s (flash erase and first
  write are slow); the dongle's receive buffer holds 32 reports instead of 8.
- Online update check: TLS buffers now live in PSRAM. With Wi-Fi, Bluetooth and USB running only about 46-52 KB of
  internal RAM was free, so GitHub connections failed at random with `ESP_ERR_HTTP_CONNECT`. A failed connection is
  retried once, and the error detail now names the hop and the host.

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
