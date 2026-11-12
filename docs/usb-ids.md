# USB identifiers

Default VID is Espressif's `0x303A` with a development PID. Never use another
vendor's VID. To obtain a dedicated PID, follow Espressif's process at
<https://github.com/espressif/usb-pids> (open a pull request adding the product).
VID/PID are configurable via Kconfig (`HUBBLE_USB_VID`, `HUBBLE_USB_PID`); the development default is
`0x303A:0x4001`. The device reports manufacturer "HubBLE Project", product "HubBLE" and the eFuse MAC as serial number.

## Requesting a dedicated PID

Espressif allocates PIDs under its VID `0x303A` to products built on its chips, free of charge for open-source projects.
The request is a pull request to <https://github.com/espressif/usb-pids>, adding one line to `allocated-pids.txt`
(keep the table's sort order; check the repository's `README` for the current rules before sending):

```text
0x303A:0xXXXX | HubBLE | USB HID dongle that bridges up to four Bluetooth LE keyboards and mice to one wired USB device | https://github.com/wildzer0/HubBLE
```

What the pull request should say:

- **Product:** HubBLE, an open-source (MIT) dongle based on the ESP32-S3 that presents BLE keyboards and mice to a PC as one
  standard USB HID device, with no driver and no host software.
- **Why a dedicated PID:** the development PID `0x4001` is shared by every prototype; operating systems cache per-VID/PID
  data (keyboard layout assignment, power policy) and users with several dongles would collide.
- **Link:** the public repository and its releases, which show the firmware that uses the PID.

When Espressif assigns the number, set it in `firmware/sdkconfig.defaults` (`CONFIG_HUBBLE_USB_PID`), record it here, and
release a new version. Existing dongles keep working but the host will see a new device the first time.
