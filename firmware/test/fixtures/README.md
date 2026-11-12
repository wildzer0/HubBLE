# Report-map fixtures

Real HID report maps exported from peripherals, used by the parser tests.

Export: web app -> Device detail -> **Export report map** (or `GET /api/v1/devices/{id}/report-map`, field `hex`).
Save as `<device>.hex` (one hex string) and add a test in `firmware/test/unit/test_hid_parser.c` that parses it.

Pending (needs the hardware, M3 gate): `yunzii-b68.hex`, `protoarc-em06.hex`.

Note: on the B68 running Oblio firmware the Bluetooth side is implemented by the BK3632 radio chip, so its report
map is the chip's, not Oblio's.
