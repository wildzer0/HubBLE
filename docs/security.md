# Security and threat model

Initial outline; extended per milestone and reviewed in M8.

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
- **OTA:** images are validated (chip, checksum/SHA-256, project name, board marker) but **not signed yet**
  (M6): anyone who can reach the API can install firmware. The rollback self-test limits damage from a broken image,
  not from a malicious one.
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
- **Physical confirmation** (default ON) requires both Shifts for 3 s on an already paired keyboard.
- **Residual risk:** an active radio MITM during the pairing window against a Just Works device is not prevented.
- Passkeys are generated with the hardware RNG, never logged and only returned by the API while being displayed.
