# USB link protocol

The configuration over USB (see [Configuring over USB](../usb-config.md)) carries the REST API and the live events of the
Wi-Fi connection over a vendor-defined HID interface. The authoritative definition is the header comment of
`firmware/components/usb_link/include/ulink_proto.h`; `webapp/src/api/usbLink.ts` mirrors it and both are tested with the same
byte vectors.

## Layers

| Layer | Definition |
|---|---|
| HID interface | Usage page `0xFF00`, usage 1; one 64-byte input and one 64-byte output report, no report IDs. VID `0x303A`. |
| Report | Byte 0 = number of valid bytes (0..63), then that many stream bytes, zero padded. |
| Frame | `[length low][length high][type][payload]`, payload at most 1100 bytes. |

## Frames

| Type | Direction | Payload |
|---|---|---|
| `0x51` HELLO | host to dongle | `{"proto":1}`; sent at connection and every 5 s as a heartbeat |
| `0x52` INFO | dongle to host | `{"proto":1,"fw":"x.y.z","board":"s3_zero","open":true}` |
| `0x01` REQ_HEAD | host to dongle | `{"id":n,"method":"GET","path":"/api/v1/...","ct":"...","len":bytes}` |
| `0x02` REQ_BODY | host to dongle | raw body chunk, at most 1024 bytes |
| `0x41` ACK | dongle to host | empty; sent after REQ_HEAD (when `len` > 0) and after every REQ_BODY except the last |
| `0x11` RES_HEAD | dongle to host | `{"id":n,"status":200,"ct":"..."}` |
| `0x12` RES_BODY | dongle to host | raw body chunk (a chunked HTTP body is decoded by the dongle) |
| `0x13` RES_END | dongle to host | empty |
| `0x21` EVENT | dongle to host | `{"event":"...","data":...}`, the same JSON as the WebSocket events |
| `0x31` LIVE | host to dongle | the JSON the web app sends over the WebSocket (`hello`, `input.subscribe`...) |
| `0x7F` ERROR | dongle to host | `{"code":"...","message":"..."}` |

The host sends one request at a time and the next body chunk only after the ACK, so the dongle needs no deep buffer. The
dongle forwards each request to its own HTTP server over the loopback interface, adding the `Host` and `X-HubBLE` headers the
API requires, so every endpoint, its validation and its guard exist once. Only `/api/v1/` paths are forwarded.

While the configuration window is closed, requests get `403 usb_locked`.
