# Device compatibility

Bluetooth keyboards, mice and trackballs differ a lot in how they describe themselves. HubBLE learns what it can from
each one, and you can help the project by sending that information when something works, or does not.

## What a report contains

The web app builds the report on the device's page and **shows it to you in full before anything is sent**. It
contains:

- the HubBLE firmware version and board;
- what the peripheral advertises: its kind (keyboard or pointer), advertised name, GAP appearance and the Device
  Information strings (manufacturer, model, firmware revision, PnP ID);
- its HID report map (the description of the reports it sends) and what HubBLE made of it: parse result, number of
  fields, whether the boot keyboard fallback was needed;
- the security level of the pairing and the negotiated connection parameters;
- whether the device exposes a battery level, and the latency figures HubBLE measured;
- an optional note, if you type one.

It does **not** contain keystrokes or any typed text, the Bluetooth address, the name you gave the device, or anything
about your Wi-Fi. The exact format is [`device-report.schema.json`](api/device-report.schema.json).

## What we do with reports

Reports are stored by a small service ([source](https://github.com/wildzer0/HubBLE/tree/main/service)) and used for two
things: to find devices that HubBLE does not handle well (so the parser can be fixed and added to its test fixtures),
and to publish the table below. Only aggregate numbers are public; the report maps themselves are not. Reports are kept
until the maintainer deletes them. The service keeps no addresses and no logs; the details are in its
[README](https://github.com/wildzer0/HubBLE/blob/main/service/README.md).

## How to send a report

1. Pair the device with HubBLE and use it for a few minutes, so the latency figures mean something.
2. Open the device on the **Devices** tab and choose **Report this device**.
3. Read the report, add a note if you want (for example "Caps Lock LED does not follow the PC"), and press **Send**.

If your dongle has no internet access (it only has its own Wi-Fi), choose **Download** instead and attach the file to an
[issue](https://github.com/wildzer0/HubBLE/issues).

## Devices reported so far

<!-- compat-table:start -->

No statistics have been published yet.

<!-- compat-table:end -->

The latency columns are the median, across reports, of the average and of the 95th percentile of the time HubBLE adds
between the Bluetooth notification and the USB report, in milliseconds. "Parse OK" is the share of reports whose report
map was understood without errors.
