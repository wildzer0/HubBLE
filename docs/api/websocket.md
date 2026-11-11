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

Planned for later milestones: `device.updated`, `device.removed`, `pairing.*`, `input.event`, `ota.result`, `ap.timeout`
(see `CLAUDE.md` §4.2).

## Limits

- Client frames larger than 128 bytes close the connection.
- Log text is only streamed while a WebSocket client is connected; older text is available from
  `GET /api/v1/logs`.
- Release builds never log keystrokes or report payloads, so none can appear here.
