# ADR 11: BLE central design

Status: accepted

## Context

CLAUDE.md §3.3 needs up to 4 bonded HID peripherals connected at once, allow-list only, with auto-reconnect.
The ESP32-S3 controller can run one initiating (connect) procedure at a time and cannot scan while initiating
with a different filter.

## Decision

- NimBLE, central + observer roles only, 5 connections (4 devices + 1 pairing candidate).
- A persistent **registry** (NVS, 4 slots) stores slot, address and name. The slot index is the merge source
  index used by `usb_hid`. Keys live in NimBLE's own bond store.
- **One outgoing connect at a time**: the *background* connect uses the controller allow-list built from
  registered + bonded + disconnected devices (`ble_gap_connect(NULL peer)`), the *pairing* connect targets one
  address. `reconcile()` picks which, and the pairing scan pauses the background connect.
- HOGP discovery starts only after the link is encrypted; unbonded links are refused. Unknown devices are
  never connected (the allow-list); a re-pairing request from a registered device is honoured only while the
  user runs the pairing flow.
- M3 uses Just Works (IO capability: none) with bonding; the secure flows of §3.4 come in M4.
- The pure parts (`hid_parser`, `hid_normalizer`, `combo`, `lat_stats`) are host-tested; `hogp_client` and
  `ble_central` are thin state machines over NimBLE callbacks.

## Consequences

- Peripherals using resolvable private addresses are not supported by the allow-list connect (the controller
  would need a resolving list). Devices with public or static random addresses work. To be verified on the B68/EM06.
- Reconnect after a peripheral sleeps relies on its directed/undirected advertising matching the allow-list.
- A new connection attempt is delayed 1 s after each disconnect to avoid a tight loop with a failing device.
