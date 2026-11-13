# Security and threat model

Threat model, extended per milestone. The M8 review is in [Security review](security-review.md).

| Threat | Planned mitigation |
|---|---|
| Attacker within radio range | WPA2-PSK AP with mandatory password change; BLE bonding + allow-list only; LE Secure Connections |
| Attacker knowing the Wi-Fi password | CSRF header on state changes, rate-limited pairing, physical confirmation for pairing/OTA |
| Malicious OTA image | SHA-256 + signed images (M6), board check, anti-downgrade, automatic rollback |
| Malicious peripheral (malformed report map) | Robust, fuzz-tested HID parser; unknown usages ignored |
| Keystroke leakage | Keystrokes never persisted or logged |

Report vulnerabilities as described in `SECURITY.md`.

## State in M1

- **Access control** is the WPA2-PSK Wi-Fi password only. The documented default password `hubble-setup` is public;
  the web app shows a notice until it is changed, and the firmware refuses to set the default again.
  Enforcing the change before anything else (setup wizard) arrives in M5.
- **CSRF:** every `POST` needs `X-HubBLE: 1` and the right content type; a cross-origin `Origin` is rejected.
- **OTA (until M5):** images were validated (chip, checksum/SHA-256, project name, board marker) but not signed: anyone
  who could reach the API could install firmware. Superseded by the M6 section below.
- **Secrets:** the Wi-Fi password is stored in NVS and never returned by the API.
- **Logs:** the log buffer contains boot/diagnostic text only; keystrokes and report payloads are never logged.

## State in M4 (secure pairing)

- **Pairing is explicit and short:** only from the web app (Wi-Fi password required), one device at a time, 60 s
  connect+pair window, 30 s challenge, 30 s confirmation; 3 failures lock pairing for 60 s.
- **Bonded devices only:** the controller allow-list contains only registered and bonded peripherals; a connection
  that is not encrypted and bonded is dropped; a re-pairing request from a known device is honoured only while the
  user is pairing.
- **Authentication levels** are shown as 🟢/🟡/🔴 (see `pairing-and-security.md`). Legacy pairing is refused by default.
- **Quarantine** keeps a Just Works candidate muted until a challenge is solved; a failed or abandoned pairing deletes
  the bond (nothing half-paired persists).
- **Physical confirmation** (default ON) requires Ctrl + Alt for 3 s on an already paired keyboard.
- **Residual risk:** an active radio MITM during the pairing window against a Just Works device is not prevented.
- Passkeys are generated with the hardware RNG, never logged and only returned by the API while being displayed.

## State in M6 (signed updates)

- **Signed images:** every OTA image carries an RSA-3072 signature (ESP32-S3 supports no ECDSA scheme for apps, ADR 6) and
  the running firmware verifies it before the new slot can be selected, for both the file upload and GitHub downloads.
  A wrong or missing signature is refused. The release key lives in a CI secret with an offline backup
  (`key-management.md`).
- **Download integrity:** HTTPS with the certificate bundle, plus the SHA-256 of `manifest.json` checked before the slot is
  selected. The manifest is not trusted for authenticity on its own: the signature is.
- **Board check and anti-downgrade:** a release without an image for this board is refused; an older release needs an
  explicit confirmation in the web app; a release whose `min_version` is above the running version must be reached in steps.
- **Physical confirmation** (default ON) is required before an online install when a keyboard is paired.
- **Update Wi-Fi credentials** are stored in NVS, write-only through the API, excluded from backups and wiped by a factory reset.
- **Residual risks:** no hardware secure boot or flash encryption, so physical access (USB re-flash) is not defended
  against; a leaked release key allows malicious updates until the key is rotated and devices are re-flashed; the API
  call that starts an update needs the Wi-Fi password but not the physical confirmation when no keyboard is paired.

## State in M8 (hardening)

Reviewed against this threat model, see [Security review](security-review.md). Added: cross-site WebSocket hijacking guard,
strict `Host` check on state-changing calls, fuzzing of the HID parser, the manifest parser and the DNS responder, and
sanitizer runs of all host tests.

## Configuration over USB

- **Threat:** a program on the host computer opens the dongle's vendor HID interface and changes settings, factory-resets it or
  sends a firmware image, without knowing the Wi-Fi password.
- **Mitigation:** the link answers only while the configuration window is open (`wifi_ap_config_open()`): the boot window,
  a physical gesture (Ctrl + Alt, mouse buttons, BOOT) or an active session. Otherwise every request gets `403 usb_locked`. A
  program cannot open the window itself. Firmware images are still checked against the release signature.
- **Surface:** the framing parser takes untrusted bytes; it is bounded (frames over 1100 bytes resynchronise), fuzzed, and the
  requests it forwards are limited to `/api/v1/` paths and the API's own validation.
