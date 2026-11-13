# Pairing and security

Pairing can only be started from the web app, lasts at most 60 s (connect + pair) and pairs one device at a time.
The dongle's IO capability is **DisplayOnly** and bonding uses **LE Secure Connections** whenever the peripheral
supports them. At most four devices can be paired; forget one to add another.

## Security levels

| Badge | Meaning | How it is reached |
|---|---|---|
| 🟢 green | LE Secure Connections + authenticated (MITM protected) | Keyboard Passkey Entry |
| 🟡 yellow | LE Secure Connections, Just Works, **verified by a challenge** | Mouse / trackball, or a keyboard that only supports Just Works |
| 🔴 red | Legacy pairing (no LE Secure Connections) | Only with the per-pairing "allow legacy" override, with a warning |

## The flows

1. **Add device**: the dongle scans and lists advertising HID devices (name, RSSI). You pick one.
2. **Keyboard (Passkey Entry)**: the web app shows a random 6-digit passkey from the hardware RNG. Type it on the
   keyboard and press Enter. Result: 🟢.
3. **Just Works devices are quarantined.** The device is connected but *nothing it sends reaches the host*. The web
   app shows a random challenge, to be solved within 30 s:
    - mouse / trackball: a sequence of 6 clicks, for example `L R R L M R`;
    - keyboard: a random 6-character code (letters and digits without I, O, 0, 1) to type.

    Wrong inputs restart the sequence; after 3 mistakes or at the timeout the bond is deleted. On success: 🟡.
4. **Physical confirmation** (setting "Require physical confirmation", default ON): the new pairing must be confirmed
   by holding **Ctrl and Alt together for 3 s on an already paired keyboard** (or holding the left and right buttons of a paired
   mouse for 3 s, or pressing the **BOOT** button on the dongle: pressing a button on the device itself is the strongest proof
   of physical presence) (within 30 s). It is skipped when no other
   keyboard is paired yet (first run). The gesture also switches the Wi-Fi AP on, which is harmless.
5. **Legacy pairing** is rejected by default (the bond is deleted and the pairing fails with `legacy_rejected`). If you
   accept the risk, start the pairing with "allow legacy"; the device is 🔴.

States: `IDLE -> SCANNING -> CONNECTING -> PAIRING -> QUARANTINE -> CONFIRM -> BONDED | FAILED` (a pure, host-tested
state machine in `firmware/components/pairing`).

## Rate limiting

Three consecutive failed pairings lock pairing for 60 s (`429 pairing_locked`, `lock_remaining_s` in the status).
Cancelling does not count as a failure; a success resets the counter.

## What the quarantine does and does NOT protect against

The challenge verifies **which device** is being paired: it proves you hold the device you meant to pair and that it
is the one talking to the dongle. It does **not** protect against an active radio man-in-the-middle during the pairing
window: Just Works has no authentication, so an attacker relaying the exchange could be paired instead. Use a
keyboard (Passkey Entry, 🟢) when that matters, and pair in a place you trust. 🟡 and 🔴 devices carry this
residual risk after pairing too, because their keys were agreed without authentication.

## API

`GET /api/v1/pairing/scan` returns the scan list and the `pairing` object (`state`, `reason`, `level`, `passkey`,
`challenge`, `remaining_s`, `lock_remaining_s`, `needs_confirmation`). `POST /api/v1/pairing/start` takes
`{ address, address_type, allow_legacy? }`. The WebSocket sends `pairing.state`, `pairing.passkey`,
`pairing.challenge` (with progress) and `pairing.result`. See `api/openapi.yaml` and `api/websocket.md`.
