# Latency

Placeholder; written in the milestone that implements it (see `CLAUDE.md` §8).

## How latency is measured (M3)

The firmware timestamps a BLE notification when it reaches the NimBLE host task and records the time again right after
the report was merged and handed to the USB task (`usb_hid_*` returned). Per device min/avg/p95/max are exposed as
`latency_us` in `/api/v1/devices` (histogram with 250 us buckets up to 16 ms). The USB endpoint transmission itself
(up to 1 ms polling) is not included. Results with the real peripherals go here after the M3 hardware gate.
