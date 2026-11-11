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
