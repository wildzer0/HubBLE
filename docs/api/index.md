# API reference

Everything the firmware and the web app expose, from the wire protocol down to single functions.

## Firmware ⇄ web app contract

| Document | What it describes |
|---|---|
| [`openapi.yaml`](openapi.yaml) | The REST API under `/api/v1`: every route, request body, response schema and error. CI checks that it matches the routes registered in the firmware. |
| [WebSocket events](websocket.md) | `ws://hubble.local/api/v1/ws`: heartbeat, status, logs, devices, pairing, input tester and update events. |
| [USB link](usb-link.md) | The same API and events carried over USB (WebHID) when the USB configuration interface is enabled. |
| [`device-report.schema.json`](device-report.schema.json) | JSON schema of the compatibility report a device page can export. |

All state-changing requests need the header `X-HubBLE: 1` and a JSON content type; errors always look like
`{ "error": { "code": "...", "message": "..." } }`.

## Source documentation (generated)

Both references are generated from the comments in the code on every documentation build, so they cannot drift from the
source. Every function, type, constant, struct field and enum value has a Doxygen / TSDoc block.

- <a href="generated/firmware/html/index.html">Firmware (Doxygen)</a>: C components under `firmware/components` and
  `firmware/main`, including the static helpers. Each file starts with a header comment (purpose, dependencies, threading).
- <a href="generated/webapp/index.html">Web app (TypeDoc)</a>: the API client, types, the USB link codec, hooks,
  components, pages and the mock API.

To build them locally see [Development](../development.md#docs).
