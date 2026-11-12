# USB identifiers

Default VID is Espressif's `0x303A` with a development PID. Never use another
vendor's VID. To obtain a dedicated PID, follow Espressif's process at
<https://github.com/espressif/usb-pids> (open a pull request adding the product).
VID/PID are configurable via Kconfig (`HUBBLE_USB_VID`, `HUBBLE_USB_PID`); the development default is
`0x303A:0x4001`. The device reports manufacturer "HubBLE Project", product "HubBLE" and the eFuse MAC as serial number.
