# HubBLE — Project Brief for the Coding Agent

> **HubBLE** is a tiny USB dongle based on the ESP32-S3 that connects to multiple
> Bluetooth Low Energy keyboards and mice at the same time and presents them to
> the host PC as a single, standard, wired USB HID device.
> The host needs **no Bluetooth, no drivers and no software**.
> A built-in **web app** (served by the dongle over its own Wi-Fi access point)
> is the killer feature: pairing, device management, diagnostics, settings and
> **over-the-air (OTA) updates** — the board should never need to be unplugged
> to be updated.

This file is the single source of truth for the project scope. Read it fully
before starting, and re-read the relevant milestone before working on it.

---

## 0. How you (the agent) must work

### 0.1 Autonomy and communication
- You own the whole lifecycle: code, tests, documentation, CI/CD, GitHub
  pushes, pull requests, tags and releases.
- **Do NOT enter plan mode** for each milestone. Instead, **before starting each
  milestone, send the user a brief summary** (max ~10 lines) using this template,
  then proceed immediately without waiting for approval:

  ```
  ▶ Milestone Mx — <name>
  What I'm going to do: <3–6 bullets>
  Release I'll tag: vX.Y.Z
  What I'll need from you: <nothing | hardware test | decision>
  ```

- **At the end of each milestone**, send a short report: what was done, the
  release link/tag, how to install it (OTA steps), and the **manual hardware
  test checklist** for that milestone.
- **Talk to the user in Italian.** Everything that goes into the repository
  (code, comments, docs, commit messages, PRs, release notes) is in **English**.
- Only stop and ask the user when:
  1. a milestone is marked **⛔ Hardware gate** (wait for the test results);
  2. an action is irreversible or security-sensitive (making the repo public,
     generating/rotating the firmware signing key, deleting releases/tags,
     force-pushing);
  3. a requirement in this file is contradictory or impossible — explain why and
     propose an alternative.
- Never invent hardware facts. Where this file says **(verify)**, check the
  datasheet/official docs/vendor page before relying on it, and record the
  finding in `docs/hardware.md`.

### 0.2 Git and GitHub workflow
- Default branch: `main` (always releasable, CI green).
- One branch per milestone: `milestone/m<N>-<short-name>`; small, focused
  commits following **Conventional Commits** (`feat(ble): …`, `fix(ota): …`,
  `docs: …`, `ci: …`, `test: …`, `refactor: …`, `chore: …`).
- Open a PR per milestone (description = summary + checklist + test evidence),
  wait for CI to be green, **squash-merge**, then tag on `main`.
- **Semantic Versioning**, tags `vMAJOR.MINOR.PATCH` (pre-releases
  `vX.Y.Z-rc.N`). Milestones before 1.0 are tagged `v0.N.0` and published as
  GitHub **pre-releases**; M8 produces `v1.0.0`.
- Pushing a tag triggers the release pipeline (see §6). Never move or delete a
  published tag; fix forward with a patch release.
- Never force-push `main`. Never commit secrets, keys or credentials.
- Maintain `CHANGELOG.md` (Keep a Changelog format) on every PR.
- **Direct-to-main policy (user decision, after v1.0.0):** no more PRs or milestone branches. Work and commit
  directly on `main`, run `tools/check.sh [quick|full]` before every push, and keep CI fast. Tags and releases still
  follow the rules above (CHANGELOG section first, never move a tag).
- Use the `gh` CLI. At M0, if the repository does not exist yet, ask the user
  for the owner/name and **visibility** (public is recommended: OTA "check for
  updates" downloads release assets from GitHub without credentials; with a
  private repo only file-upload OTA works).

### 0.3 Definition of Done (every milestone)
- [ ] Features of the milestone implemented as specified.
- [ ] Code formatted, linted, warnings-as-errors clean (firmware and web app).
- [ ] Unit tests added/updated and passing in CI; coverage of pure logic
      (parsers, state machines, normalizers) ≥ 80 %.
- [ ] Every public function documented (Doxygen / TSDoc); see §5.
- [ ] Docs updated (`docs/`, `README.md`, `CHANGELOG.md`, API spec if changed).
- [ ] Firmware and web app size budgets respected (§4.9).
- [ ] Manual hardware test checklist written in `docs/testing/m<N>.md`.
- [ ] PR merged, tag pushed, release published with all assets.

---

## 1. Product requirements

### 1.1 Goals
1. Up to **4 BLE HID peripherals** (keyboards, mice, trackballs) connected
   **simultaneously**, forwarded to the host as one composite USB HID device.
2. **Zero host requirements**: works in BIOS/UEFI, at login screens, on locked
   corporate PCs, on any OS.
3. **Secure pairing**, Logitech-style physical verification (§3.4).
4. **Web app** for everything (§3.7), reachable from a phone over the
   dongle's own Wi-Fi access point.
5. **OTA updates are essential**: after the very first flash, all updates are
   done over the air. Robust rollback; the dongle must never be bricked by an
   update.
6. Low latency: target end-to-end added latency ≤ 15 ms (BLE notification →
   USB report) at 7.5 ms connection interval, with Wi-Fi off.

### 1.2 Non-goals (v1)
- Bluetooth Classic devices and audio (headsets) — not supported by the ESP32-S3.
- Proprietary 2.4 GHz receivers (Logitech Unifying/Bolt, etc.).
- Key remapping, macros, profiles, pointer acceleration (possible future work).
- Multi-host switching (possible future work, e.g. via ESP-NOW between two dongles).

### 1.3 Target peripherals for validation
- **YUNZII B68** keyboard (Bluetooth 5.0 mode; the user runs a **custom
  firmware** on it — ask the user which one when you reach M3).
- **ProtoArc EM06** trackball (use its Bluetooth 5.x channel; if it also has a
  BT 3.0/Classic channel, that one is not supported).

---

## 2. Hardware

### 2.1 Primary board: ESP32-S3 "Zero"-style board (Waveshare ESP32-S3-Zero clone)
- SoC: **ESP32-S3FH4R2** — 4 MB flash (in-package), 2 MB quad PSRAM.
  (verify: chip marking must read `ESP32-S3`; the vendor listing also mentions
  "ESP32-C3-Zero", which would NOT work — no USB OTG.)
- On-board **ceramic antenna** (no external antenna).
- USB-C connected to the S3's native USB (GPIO19/20) — used both for flashing
  (USB-Serial-JTAG / ROM download mode) and, at runtime, for USB OTG (TinyUSB).
- On-board addressable **WS2812 RGB LED**, expected on **GPIO21** (verify).
- BOOT button (GPIO0) and RESET button. The BOOT button is **only** a last-resort
  recovery path (§3.10); no normal feature may require it.

### 2.2 Secondary board: Seeed Studio XIAO ESP32-S3
- 8 MB flash, 8 MB PSRAM, external U.FL antenna, single user LED (GPIO21,
  active-low — verify).
- Must be supported as a second build target (separate `sdkconfig` defaults and
  partition table), built in CI, released as separate assets.

### 2.3 Board abstraction
- Board selection via Kconfig (`CONFIG_HUBBLE_BOARD_S3_ZERO`,
  `CONFIG_HUBBLE_BOARD_XIAO_S3`), with a `board` component exposing LED type,
  LED GPIO, flash size and button GPIO. No GPIO numbers hard-coded elsewhere.

---

## 3. Functional specification

### 3.1 Architecture overview

```
 BLE keyboards/mice ──► [NimBLE central + HOGP client] ──► [report parser/normalizer]
                                                                   │
                                     [combo detector] ◄────────────┤
                                                                   ▼
 Phone ◄──Wi-Fi AP──► [HTTP + WebSocket server] ◄─► [core / event bus] ──► [TinyUSB composite HID] ──► Host PC
                         │                              │
                     [OTA manager]                [settings (NVS)]   [LED]   [power manager]
```

- Language: **C (C17)** on **ESP-IDF** (pin the latest stable 5.x release at M0,
  verify; pin the same version in CI via the official Docker image /
  `espressif/esp-idf-ci-action`).
- BLE stack: **NimBLE** (smaller, multi-connection central).
- USB: **TinyUSB** via the `esp_tinyusb` managed component.
- Components under `firmware/components/`, each with a narrow public header:
  `board`, `led`, `settings`, `event_bus`, `ble_central`, `hogp_client`,
  `hid_parser`, `hid_normalizer`, `usb_hid`, `bridge`, `combo`, `pairing`,
  `wifi_ap`, `captive_portal`, `web_server`, `api`, `ota`, `power`, `diag`,
  `log_buffer`.
- Pure-logic components (`hid_parser`, `hid_normalizer`, `combo`, pairing
  state machine, settings (de)serialization, version comparison, OTA manifest
  parsing) must **not** depend on ESP-IDF APIs so they can be unit-tested on the
  host (§5.3).
- Use FreeRTOS tasks with clear ownership; the HID forwarding path must never
  block on Wi-Fi/HTTP work. Prefer queues/event groups over shared state; any
  shared state must be protected and documented.

### 3.2 USB HID device
- Composite HID device with these reports:
  - **Keyboard**: boot-protocol compatible (so it works in BIOS/UEFI);
    6KRO is acceptable for v1, NKRO is a stretch goal.
  - **Consumer control** (media keys, volume, brightness): 16-bit usage.
  - **Mouse**: 5 buttons, 16-bit X/Y, vertical wheel, horizontal pan (AC Pan).
  - **System control** (sleep/wake) — optional.
- Input from **all** connected peripherals is merged: keyboard key states are
  combined (union of pressed keys/modifiers per device, then merged), mouse
  deltas are summed per report interval.
- Endpoint polling interval: 1 ms (full speed).
- **Remote wakeup** supported (§3.9).
- USB strings: manufacturer "HubBLE Project", product "HubBLE". VID/PID
  configurable via Kconfig; default to Espressif's VID `0x303A` with a
  development PID, and document in `docs/usb-ids.md` how to request a dedicated
  PID from Espressif. Never use another vendor's VID.
- **No CDC/serial interface in release builds** (corporate PCs may flag it). A
  `debug` build variant may add CDC for logs.

### 3.3 BLE central (HOGP)
- NimBLE central, up to **4 simultaneous connections** (+ headroom for one
  pairing candidate).
- HID-over-GATT client: discover HID service (0x1812), read the **Report Map**,
  subscribe to input reports (report protocol), read Battery Service (0x180F)
  and Device Information (0x180A); handle output reports (keyboard LEDs:
  Caps/Num/Scroll Lock state from the host must be forwarded to every connected
  keyboard).
- A **generic HID report-descriptor parser** (robust, fuzz-tested) translates
  arbitrary report maps into the dongle's normalized internal events
  (key down/up, modifiers, consumer usage, mouse buttons/deltas/wheel/pan).
  Unknown usages are ignored, never crash.
- **Bonding only, allow-list only**: only bonded peripherals may connect;
  connections from unknown devices are rejected. Bonds persisted in NVS.
- **Auto-reconnect** to bonded devices (background scan with the allow-list
  filter), including after the peripheral's own sleep.
- Connection parameters: request 7.5 ms interval (min/max 6 units), peripheral
  latency per §3.9, supervision timeout ~2 s; measure the **actual** negotiated
  values and expose them.
- Collect per-device stats: RSSI, connection interval, latency (§3.11),
  reconnect count, last seen, battery level.
- Wi-Fi/BLE coexistence enabled; document the latency impact while the AP is on.

### 3.4 Secure pairing (Logitech-style)
Pairing can only be started **from the web app**, lasts at most **60 s**, and
pairs **one device at a time**.

1. Web app → "Add device" → dongle scans and lists nearby advertising HID
   devices (name, RSSI, appearance icon). The user picks one.
2. Pairing uses **LE Secure Connections**. The dongle's IO capability is
   **DisplayOnly**.
   - **Keyboards** (Passkey Entry): the web app shows a random 6-digit
     passkey; the user types it on the keyboard + Enter. Result: authenticated
     (MITM-protected) bond. Security level 🟢.
   - **Mice/trackballs** (Just Works at BLE level) — and keyboards that only
     support Just Works: the device enters **quarantine** — it is connected but
     **nothing is forwarded to the host**. The web app shows a random
     **challenge**:
     - mouse: a sequence of 5–6 clicks (e.g. L R R L M R), to be performed
       within 30 s;
     - keyboard: a random 6-character code to type.
     On success → bond kept, security level 🟡. On failure/timeout → bond
     deleted. The docs must state clearly that this verifies *which* device is
     being paired but does not protect against an active radio MITM during the
     pairing window.
   - **Legacy pairing** (no LE Secure Connections) is rejected by default; the
     user may enable a per-pairing "allow legacy" override with a warning →
     security level 🔴.
3. Optional setting **"Require physical confirmation"** (default ON): a new
   pairing (and an OTA install, §3.8) must be confirmed by holding **both
   Shift keys for 3 s on an already-paired keyboard**. Skipped automatically
   when no keyboard is bonded yet (first-run).
4. The pairing flow is a pure state machine (host-testable) with explicit
   states: `IDLE → SCANNING → CONNECTING → PAIRING → QUARANTINE → CONFIRM →
   BONDED | FAILED`.

### 3.5 Wi-Fi access point
- SoftAP, SSID `HubBLE-XXXX` (last 4 hex digits of the MAC), **WPA2-PSK**.
- **First run**: documented default setup password; the web app's setup wizard
  **forces** the user to set a new password (min 10 chars) before anything else.
- The AP is **off** during normal use (latency/coexistence). It turns on when:
  1. **at power-up**, for a configurable window (default 5 min);
  2. **both Shift keys held for 3 s** (no other keys) on any paired keyboard
     (the key events are still forwarded normally — they are harmless);
  3. always, while **no device is bonded** (first-run convenience).
- While at least one client is connected **and** the web app is open (WebSocket
  heartbeat), the AP stays on. After the last client leaves, it turns off after
  a configurable idle timeout (default 3 min). OTA in progress always keeps it on.
- **Captive portal**: DNS server answering all queries with the dongle IP plus
  the platform probe URLs (iOS, Android, Windows), so the web app opens
  automatically on connection. Also **mDNS**: `http://hubble.local`.
- Optional **station (STA) credentials** (e.g. home Wi-Fi or a phone hotspot)
  used **only** for "check for updates" (§3.8), in APSTA mode, on demand.

### 3.6 LED status (configurable brightness, can be disabled)
| State | Pattern (RGB LED; single-LED boards use blink codes) |
|---|---|
| Booting | white, short |
| Normal, ≥1 device connected | off (or dim green if "status LED" enabled) |
| No device connected | slow amber pulse |
| AP on | blue breathing |
| Pairing in progress | fast cyan blink |
| Quarantine/challenge | magenta blink |
| OTA in progress | purple fast blink |
| Error / rollback happened | red, 3 blinks repeated |
| Host suspended | off |

### 3.7 Web app (the killer feature)
**Stack**: Vite + **Preact** + **TypeScript** (strict), plain CSS (CSS
variables, light/dark), no heavy UI frameworks. Mobile-first (phone is the
primary client), accessible (keyboard navigation, ARIA, contrast). UI strings
via a tiny i18n layer with **English (default) and Italian**.
The production build is **gzipped and embedded into the firmware image**
(served with `Content-Encoding: gzip`), so one OTA updates firmware and web app
together and they can never be out of sync.

**Pages**
1. **Setup wizard** (first run): set the Wi-Fi password → generate the
   **printable QR sticker** (Wi-Fi QR + `hubble.local` URL) → pair the first device.
2. **Dashboard**: device cards (name, type icon, connected state, battery,
   RSSI, security badge 🟢🟡🔴, measured latency), dongle status (host USB
   state, AP timeout countdown, firmware version), "Add device" button.
3. **Pairing flow**: scan list → passkey / challenge screens with live feedback
   and countdown → result.
4. **Device detail**: rename, forget, connection parameters, stats, battery
   history (session only), **export report map** (for debugging and test fixtures).
5. **Input tester**: live view of keys/clicks/movement arriving from each
   device (only while the page is open and explicitly enabled; never stored).
6. **Settings**: Wi-Fi (password, AP boot window, idle timeout, channel),
   STA credentials for updates, Shift-combo hold time, physical confirmation,
   LED brightness, remote wakeup, allow-legacy policy, language.
7. **Update**: current version, other OTA slot version, upload `.bin`,
   check GitHub for updates (stable / pre-release channel), release notes,
   progress bar, result, rollback information.
8. **System**: diagnostics (uptime, heap/PSRAM, partition info, reset reason,
   USB state, BLE stats, coexistence state), **live logs** + download,
   backup/restore settings (JSON; bonds are **not** exported — they contain
   keys), reboot, **reboot to USB download mode**, factory reset (with
   confirmation).

**Development without hardware**: a mock API (e.g. MSW or a Vite dev-server
plugin) implementing the full REST + WebSocket contract with realistic fake
devices, so the web app can be developed and end-to-end tested in CI.

### 3.8 OTA updates (essential)
- Partition table with **two OTA slots** and **rollback** enabled
  (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`): a new image boots in
  "pending verify" state and is marked valid only after a **self-test**
  (USB HID initialized, BLE stack up, web server up, settings readable) within
  60 s; otherwise the bootloader rolls back automatically on the next reset.
  Firmware also tracks a boot-failure counter.
- **Update sources**:
  1. **Upload** of a `.bin` from the web app (streamed to flash, never buffered
     whole in RAM).
  2. **Check for updates on GitHub**: the dongle connects as STA (APSTA) using
     the saved credentials, queries the GitHub Releases API for the repo,
     reads the release `manifest.json`, downloads the asset for its board over
     **HTTPS** (ESP x509 certificate bundle; follow redirects), installs it.
- **Integrity and authenticity**: verify SHA-256 from the manifest; release
  images are **signed** (ECDSA, "signed app images without hardware secure
  boot" — verify the exact ESP-IDF option) and the dongle refuses unsigned or
  wrongly-signed images. The signing private key lives only in a GitHub
  Actions secret; ask the user before generating it and make sure they keep an
  offline backup (losing it means no more OTA without USB re-flashing).
  Document local development signing in `docs/development.md`.
- **Anti-downgrade**: refuse older versions unless "allow downgrade" is
  explicitly confirmed in the UI.
- Board check: refuse images built for another board (board ID in the app
  description / manifest).
- During OTA: HID forwarding keeps working; AP stays on; progress over WebSocket.
- Optional physical confirmation (§3.4.3) before installing.
- **Reboot to USB download mode** from the web app (force ROM download boot),
  so a USB re-flash never requires pressing BOOT.

**Partition table — 4 MB (S3-Zero)** (adjust only with good reason; document it):

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0x10000 (64 KB) |
| otadata | data/ota | 0x19000 | 0x2000 |
| phy_init | data/phy | 0x1B000 | 0x1000 |
| ota_0 | app/ota_0 | 0x20000 | 0x1E0000 (1.875 MB) |
| ota_1 | app/ota_1 | 0x200000 | 0x1E0000 (1.875 MB) |
| coredump | data/coredump | 0x3E0000 | 0x10000 |
| spare | data (reserved) | 0x3F0000 | 0x10000 |

The 8 MB XIAO variant uses larger slots (e.g. 3 MB each) with the same layout logic.

### 3.9 Power / sleep (follows the host)
- **Host suspends USB** → dongle enters *low-power mode*: requests a longer
  BLE connection interval (e.g. 30–50 ms) and higher peripheral latency so
  peripherals save battery, turns the LED and AP off.
- **Remote wakeup** (setting, default ON): any key press / click / movement
  on a connected peripheral wakes the host (only if the host enabled remote
  wakeup). The waking event itself is **not** forwarded.
- **Host resumes** → restore 7.5 ms interval immediately.
- Document limitations: some PCs cut USB power in sleep/hibernate or disable
  USB wake via BIOS/IT policy.

### 3.10 Recovery paths (last resort, documented)
- Forgotten Wi-Fi password: hold **both Shifts + Esc for 10 s** on a paired
  keyboard → AP password reset to the default setup password and the setup
  wizard is shown again (bonds are kept).
- Full factory reset without the web app: hold **BOOT** while plugging the
  dongle in and keep it pressed for 10 s (LED feedback).
- Bricked/rolled-back image: rollback is automatic; otherwise "reboot to
  download mode" or BOOT+plug, then the web installer (§6.4).

### 3.11 Diagnostics and latency
- Measure in firmware: timestamp at BLE notification reception → timestamp at
  USB report queued; keep min/avg/p95/max per device; expose via API and UI.
- Ring-buffer logger (RAM, e.g. 16 KB) streamed to the web app; **never log
  keystrokes or report payloads** in release builds.
- Core dump to flash on crash, downloadable from the System page.

### 3.12 Security & privacy rules (non-negotiable)
- No keystroke data is ever persisted or logged; the input tester streams only
  while explicitly enabled and the page is open.
- All state-changing API calls require a custom header (`X-HubBLE: 1`) and a
  JSON content type (simple CSRF protection); reject cross-origin requests.
- Wi-Fi password and STA credentials stored in NVS; never returned by the API.
- Rate-limit the passkey/challenge flows; lock pairing for 60 s after 3
  consecutive failures.
- Keep a `docs/security.md` threat model (attacker within radio range, attacker
  knowing the Wi-Fi password, malicious OTA image, malicious peripheral sending
  malformed report maps) and how each is mitigated.

---

## 4. API contract (firmware ⇄ web app)

Document it in `docs/api/openapi.yaml` (REST) and `docs/api/websocket.md`
(events). Version prefix `/api/v1`. JSON everywhere, errors as
`{ "error": { "code": "...", "message": "..." } }`.

### 4.1 REST (minimum)
| Method | Path | Purpose |
|---|---|---|
| GET | `/api/v1/status` | version, board, uptime, USB state, AP timeout, setup required |
| GET | `/api/v1/devices` | bonded devices + live state |
| PATCH | `/api/v1/devices/{id}` | rename |
| DELETE | `/api/v1/devices/{id}` | forget (delete bond) |
| GET | `/api/v1/devices/{id}/report-map` | raw report map (hex) |
| POST | `/api/v1/pairing/scan` | start scan |
| POST | `/api/v1/pairing/start` | `{ address }` start pairing |
| POST | `/api/v1/pairing/cancel` | cancel |
| GET/PUT | `/api/v1/settings` | read/update settings (no secrets returned) |
| POST | `/api/v1/settings/wifi-password` | change AP password |
| GET | `/api/v1/backup` / POST `/api/v1/restore` | settings JSON |
| POST | `/api/v1/ota/upload` | binary stream upload |
| POST | `/api/v1/ota/check` | check GitHub for updates |
| POST | `/api/v1/ota/install` | install the found release |
| GET | `/api/v1/ota/status` | slots, versions, last result |
| GET | `/api/v1/diagnostics` | system stats |
| GET | `/api/v1/logs` / `/api/v1/coredump` | downloads |
| POST | `/api/v1/system/reboot` · `/reboot-download` · `/factory-reset` | system actions |

### 4.2 WebSocket `/api/v1/ws`
Events (server → client): `device.updated`, `device.removed`,
`pairing.state`, `pairing.passkey`, `pairing.challenge`, `pairing.result`,
`input.event` (tester only), `log.line`, `ota.progress`, `ota.result`,
`system.status`, `ap.timeout`. Client → server: `hello` (heartbeat that keeps
the AP alive), `input.subscribe` / `input.unsubscribe`.

### 4.3 Size budgets (enforced in CI)
- Firmware app image (S3-Zero): **≤ 1.70 MB** (90 % of the OTA slot).
- Web app, gzipped total: **≤ 150 KB**.
- Free heap after boot with AP on: ≥ 60 KB internal RAM.

---

## 5. Code quality

### 5.1 General
- Follow ESP-IDF coding style; enforce with **clang-format** (config in repo)
  and **clang-tidy**/**cppcheck**; `-Wall -Wextra -Werror` on project code.
- Web app: **ESLint** + **Prettier**, TypeScript `strict`, no `any`.
- No magic numbers: named constants or Kconfig options.
- Check and handle every `esp_err_t`; use `ESP_RETURN_ON_ERROR`-style macros;
  no silent failures.
- Static allocation preferred on the hot path; no dynamic allocation in the
  HID forwarding path after init.

### 5.2 Comments — as thorough as possible (explicit user requirement)
- Every source file starts with a header comment: purpose, responsibilities,
  dependencies, threading model.
- Every function (public **and** static) has a Doxygen/TSDoc block: what it
  does, parameters, return values/errors, side effects, thread-safety.
- Inline comments explain **why** (protocol constraints, spec references such
  as "HOGP v1.0 §4.x", timing assumptions, hardware quirks), not just what.
- Every struct field and enum value is documented.
- Generate API docs with **Doxygen** (firmware) and **TypeDoc** (web app) in CI.

### 5.3 Testing
- **Host unit tests** (ESP-IDF `linux` target or a plain CMake + Unity setup)
  for all pure-logic components; run in CI.
- **HID parser**: tests with real report maps as fixtures — ask the user (M3)
  to export the B68 and EM06 report maps from the Device detail page and add
  them to `firmware/test/fixtures/`. Also fuzz the parser (libFuzzer on host)
  in CI for a bounded time.
- **Web app**: Vitest unit tests + **Playwright** end-to-end tests against the
  mock API (pairing flows, OTA flow, setup wizard), mobile viewport.
- **Manual hardware checklists** per milestone in `docs/testing/`.

---

## 6. CI/CD (GitHub Actions)

### 6.1 `ci.yml` — on every push and PR
1. Lint: clang-format check, clang-tidy/cppcheck, ESLint, Prettier,
   markdownlint, actionlint.
2. Web app: install (lockfile), type-check, unit tests, build, Playwright e2e,
   gzip size check.
3. Firmware: build for **both boards** with the pinned ESP-IDF image (the web
   app build is an input of the firmware build), size check, host unit tests,
   bounded fuzzing.
4. Docs: Doxygen + TypeDoc + docs site build (no broken links).
5. Upload firmware binaries as workflow artifacts.

### 6.2 `release.yml` — on tag `v*`
1. Re-run the full build/test matrix.
2. Version comes from the tag (embedded as `PROJECT_VER`; fail if the tag and
   `CHANGELOG.md` disagree).
3. **Sign** the OTA images with the key from secrets.
4. Create the GitHub Release (pre-release if `v0.*` or `-rc.N`) with:
   - `hubble-<board>-vX.Y.Z-ota.bin` (signed app image for OTA),
   - `hubble-<board>-vX.Y.Z-factory.bin` (merged bootloader + partition table +
     app, for the first USB flash),
   - `manifest.json` (version, board IDs, sizes, SHA-256, minimum version,
     release notes URL),
   - `SHA256SUMS`,
   - release notes extracted from `CHANGELOG.md`.
5. Deploy the docs site (GitHub Pages).

### 6.3 Repository hygiene
- Dependabot (GitHub Actions, npm), pinned action versions by SHA,
  `CODEOWNERS`, issue/PR templates, `SECURITY.md`, `CONTRIBUTING.md`,
  `LICENSE` (MIT unless the user says otherwise — mention it in the M0 summary).

### 6.4 Web installer (first flash without tools)
On the docs site, an **ESP Web Tools** install page (Chrome/Edge Web Serial)
that flashes the latest `factory.bin` for the selected board. This is the only
time USB flashing is needed; everything after is OTA.

---

## 7. Repository layout

```
/
├── CLAUDE.md                 # this file
├── README.md
├── CHANGELOG.md
├── LICENSE, SECURITY.md, CONTRIBUTING.md
├── firmware/
│   ├── CMakeLists.txt, sdkconfig.defaults, sdkconfig.defaults.<board>
│   ├── partitions/           # partitions_4mb.csv, partitions_8mb.csv
│   ├── main/
│   ├── components/           # see §3.1
│   └── test/                 # host unit tests, fixtures, fuzzers
├── webapp/
│   ├── src/ (pages, components, api client, i18n, mocks)
│   └── tests/ (unit, e2e)
├── docs/
│   ├── index.md, getting-started.md (first flash + setup), user-guide.md
│   ├── hardware.md, architecture.md, pairing-and-security.md, security.md
│   ├── ota-and-recovery.md, power.md, latency.md, usb-ids.md
│   ├── api/ (openapi.yaml, websocket.md)
│   ├── development.md, release-process.md, troubleshooting.md
│   ├── adr/                  # Architecture Decision Records
│   └── testing/              # manual hardware checklists per milestone
├── tools/                    # scripts (sign, merge bin, manifest, size check)
└── .github/ (workflows, templates, dependabot.yml)
```

Record significant decisions as ADRs (`docs/adr/NNNN-title.md`): ESP-IDF vs
Arduino, NimBLE, Preact, embedding the web app, partition layout, signing
scheme, pairing model.

---

## 8. Milestones

Each milestone ends with a tagged pre-release. **OTA comes first** so that,
from M1 on, every new version is installed over the air.

### M0 — Project bootstrap → `v0.1.0`
- Repo creation (ask owner/name/visibility), license, templates, layout (§7).
- ESP-IDF project for both boards, board abstraction, LED driver, boot banner
  with version; web app skeleton with mock API; docs site skeleton; ADRs.
- Full CI (§6.1) and release pipeline (§6.2, signing can be stubbed until M6).
- `docs/getting-started.md` with first-flash instructions.

### M1 — Wi-Fi AP, web server and OTA foundations → `v0.2.0` ⛔ Hardware gate
- SoftAP with boot window + idle timeout + WebSocket heartbeat, default
  password + change endpoint, captive portal, mDNS.
- HTTP server serving the embedded gzipped web app; minimal Update and System pages.
- OTA upload with streaming, rollback + self-test, boot-failure counter,
  reboot to download mode, live logs, diagnostics.
- **Gate**: the user flashes `factory.bin` over USB **once** (web installer),
  then verifies an OTA update to a test build (`v0.2.1-rc.1`) and a forced
  rollback. Do not continue until confirmed.

### M2 — USB composite HID → `v0.3.0`
- TinyUSB composite device (§3.2), boot keyboard, consumer, mouse with pan,
  LED output reports, suspend/resume detection, remote-wakeup plumbing.
- Debug-only "inject test input" API to verify USB without BLE.
- Self-test extended to USB. Verify that OTA and "reboot to download mode"
  still work with TinyUSB owning the USB PHY.

### M3 — BLE central and forwarding → `v0.4.0` ⛔ Hardware gate
- NimBLE central, HOGP client, generic report-map parser + normalizer, merge
  logic, multi-device (4), bonding (temporary simple pairing allowed), allow-list,
  auto-reconnect, battery, connection params, latency measurement, keyboard LED
  sync, double-Shift combo → AP on, Shift+Shift+Esc recovery.
- Ask the user about the B68 custom firmware; collect report-map fixtures.
- **Gate**: user validates B68 + EM06 together (typing, media keys, trackball,
  scroll, latency numbers).

### M4 — Secure pairing → `v0.5.0`
- Full §3.4: passkey entry, quarantine + challenges, legacy policy, physical
  confirmation, rate limiting, security badges, state machine with tests.

### M5 — Complete web app → `v0.6.0`
- All pages of §3.7, setup wizard, QR sticker, input tester, settings,
  backup/restore, factory reset, i18n EN/IT, accessibility pass, Playwright
  coverage of all flows.

### M6 — OTA from GitHub + signed images → `v0.7.0`
- Ask the user before generating the signing key; store it as a GitHub secret;
  user keeps an offline backup.
- STA credentials, APSTA "check for updates", manifest, HTTPS download,
  signature + SHA-256 + board + anti-downgrade checks, stable/pre-release
  channel, optional physical confirmation, docs for key management.
- Migration note: devices on ≤ v0.6.x accept the first signed image; from
  v0.7.0 unsigned images are refused.

### M7 — Power management → `v0.8.0`
- §3.9 complete, LED/AP behaviour in suspend, settings toggle, measurements
  documented in `docs/power.md`.

### M8 — Hardening and 1.0 → `v1.0.0`
- Long-running stability test plan (24 h, reconnect storms, OTA during heavy
  input), memory leak checks, fuzzing time increased, latency report
  (`docs/latency.md`), security review against `docs/security.md`, docs
  proofreading, final user guide with screenshots (from the mock web app).

---

## 9. Future ideas (do not implement unless asked)
Key remapping and layers, pointer/scroll tuning and drag-scroll for
trackballs, profiles, multi-host switching between two HubBLE dongles over
ESP-NOW, proprietary 2.4 GHz support on an nRF52840 variant, NKRO.