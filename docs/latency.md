# Latency

Target (CLAUDE.md §1.1): at most **15 ms added** between a BLE notification and the USB report, at a 7.5 ms connection
interval, with Wi-Fi off. The latency of the radio link and of the USB polling is not under the dongle's control; this
page defines what is measured and how to reproduce it.

## What the dongle measures

The firmware timestamps a notification when it reaches the NimBLE host task and again right after the report was merged and
handed to the USB task (`usb_hid_*` returned). Per device the minimum, average, 95th percentile and maximum are kept in a
histogram with 250 us buckets up to 16 ms and shown on the **device page** and the dashboard (`latency_us` in
`GET /api/v1/devices`). Not included: the time a notification waits in the peripheral and the controller (up to one
connection interval, 7.5 ms at the default) and the USB endpoint transmission (1 ms polling). So the end-to-end figure
is roughly: **peripheral wait (0..7.5 ms) + added latency (measured) + USB polling (0..1 ms)**.

## Design choices that keep it low

- The BLE callback decodes and merges in place and wakes the USB report task; there is no dynamic allocation and no
  Wi-Fi/HTTP work on that path. The report task coalesces mouse deltas and sends keyboard changes immediately.
- 7.5 ms interval, latency 0 and a 2 s supervision timeout are requested from every peripheral; the negotiated values
  are shown per device.
- The Wi-Fi access point is off in normal use (it shares the 2.4 GHz radio with Bluetooth). It turns on on demand and off again
  after the idle time; the input tester is rate limited (about 50 events/s) and runs on a low-priority task.
- Low-power parameters (40 ms interval, latency 10) are used only while the host is suspended, and the 7.5 ms profile is
  restored immediately on resume.

## How to measure (hardware needed)

1. Pair the B68 and EM06. Open the device page, note `connection.interval_ms` and `latency` of each device.
2. Type a paragraph on the B68 and move the trackball for a minute: read min/avg/p95/max.
3. Repeat with the Wi-Fi AP on and the web app open (input tester running), and with the phone far from the dongle.
4. For an independent end-to-end number, a second computer or a hardware timer: press the key while a logic analyzer or a
   USB sniffer watches the HID endpoint, or compare timestamps of a key injected with the `debug_usb` overlay with the
   host event time. Record the setup.

## Results

The first real measurements come from two device reports exported from the web app (firmware 1.3.0, board `s3_zero`,
two devices connected at the same time). They show the **added latency inside the dongle** only (see "What the dongle
measures"); the state of the Wi-Fi access point during the session was not recorded, and the 95th percentile is
quantised to the 250 us histogram buckets. They say nothing yet about four devices or about the radio under load.

| Device | Interval / latency / timeout accepted | Added latency avg / p95 / max (us) | Samples | Same with AP on |
|---|---|---|---|---|
| OBLIO keyboard | 7.5 ms / 48 / 3000 ms (set by the keyboard) | 198 / 500 / 1401 | 1546 | not measured |
| ProtoArc EM06 | 7.5 ms / 0 / 2000 ms | 163 / 250 / 811 | 16255 | not measured |

The keyboard replaces the requested peripheral latency (0) and timeout (2 s) with its own update request and the dongle
accepts it. This does not slow down key presses, since the keyboard wakes itself; it can delay output reports to the
keyboard (for example the Caps Lock LED) by up to 48 connection intervals (about 360 ms).

Still to measure (`docs/testing/m3.md` section F, `docs/testing/stability.md` S4): AP on, four devices at once, and an
independent end-to-end figure.

## Typing filter

With **Settings → Typing filter** on, a *hold time* delays every key and button by that long (that is its purpose), and the
*bounce window* does not delay presses. With the filter off, which is the default, nothing is added to the numbers above.
