# WebSocket events

Endpoint: `ws://hubble.local/api/v1/ws` (or `ws://192.168.4.1/api/v1/ws`). Text frames, JSON.

## Heartbeat (client → server)

```json
{ "event": "hello" }
```

Send one right after connecting and then every **5 s**. While at least one Wi-Fi client is connected and a
heartbeat is younger than **15 s**, the dongle keeps its access point on. The client must reconnect
automatically if the socket closes (the web app does).

## Events (server → client)

Every message is `{ "event": "<name>", "data": <payload> }`.

| Event | Sent | `data` |
|---|---|---|
| `system.status` | every 1 s while a client is connected | `{ "uptime_s": 1234, "heap_free": 142000, "ap_timeout_s": 120 \| null }` |
| `log.line` | every 1 s when new log text exists | `{ "text": "I (1234) web: ...\n" }` (complete lines, may contain several) |
| `ota.progress` | during an upload, about every 2 % | `{ "received": 100000, "total": 870000 }` |
| `ota.state` | on every state change of the online update | the `online` object, see [Online updates](#online-updates-m6) |
| `device.updated`, `device.removed` | when a device changes | see [Device events](#device-events-m3) |
| `pairing.state`, `pairing.passkey`, `pairing.challenge`, `pairing.result` | during a pairing | see [Device events](#device-events-m3) |
| `input.event` | only while the input tester is subscribed | see [Input tester](#input-tester-m5) |

`CLAUDE.md` §4.2 also lists an `ap.timeout` event. It does not exist as a separate message: the remaining time of the
access point is the `ap_timeout_s` field of `system.status` (`null` when no countdown runs).

Client → server messages are `hello`, `input.subscribe` and `input.unsubscribe`. Anything else is ignored.

## Limits

- Client frames larger than 128 bytes close the connection.
- Log text is only streamed while a WebSocket client is connected; older text is available from
  `GET /api/v1/logs`.
- Release builds never log keystrokes or report payloads, so none can appear here.

## Device events (M3)

- `device.updated`: the device object of `GET /api/v1/devices` (id, name, connected, ready, battery, rssi, connection, latency_us...).
  Sent on connect/disconnect, discovery done, battery change, rename.
- `device.removed`: `{ "slot": n, "removed": true }` after a device was forgotten.
- `pairing.state`: the pairing status object (`state`, `reason`, `level`, `device`, `remaining_s`, `lock_remaining_s`,
  `needs_confirmation`, and when present `passkey` / `challenge`), sent on every change.
- `pairing.passkey`: same object, sent while a 6-digit `passkey` must be typed on the keyboard.
- `pairing.challenge`: same object, sent in quarantine and on every progress step (`challenge.kind` is `clicks` or `code`,
  `challenge.value` the sequence (`L`/`R`/`M`) or the code, `challenge.progress`/`length`).
- `pairing.result`: same object when the pairing ended (`state` `bonded` with `level`, or `failed` with `reason`).

## Input tester (M5)

Client → server: `{ "event": "input.subscribe" }` starts the stream, `{ "event": "input.unsubscribe" }` stops it. A
subscription **expires 15 s** after the last `input.subscribe`, so the web app renews it every 5 s and a closed page
stops the stream by itself. Only subscribed sockets receive `input.event`; nothing is stored or logged.

Server → client `input.event`, `data` is one of:

| `kind` | Fields |
|---|---|
| `key` | `slot`, `modifiers` (bit mask, bit n = usage 0xE0+n), `keys` (array of HID usage ids, page 0x07; empty = released) |
| `consumer` | `slot`, `usage` (0 = released) |
| `mouse` | `slot`, `buttons` (bit mask), `dx`, `dy`, `wheel`, `pan` |

Mouse events are paced to about 50 per second and dropped when the queue is full: the tester is best effort and never
slows down the USB forwarding path. Quarantined (unpaired) devices are not shown.

## Online updates (M6)

- `ota.state`: the `online` object of `GET /api/v1/ota/status`: `state` (`idle`, `connecting`, `checking`, `available`, `up_to_date`,
  `confirm`, `downloading`, `installed`, `error`), `error` (when `state` is `error`), `channel`, `release`
  (`version`, `min_version`, `notes_url`, `size`, `signed`, `downgrade`), `received`/`total` while downloading and
  `confirm_remaining_s` while waiting for the physical confirmation. Sent on every state change and download step.
