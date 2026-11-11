# USB identifiers

Default VID is Espressif's `0x303A` with a development PID. Never use another
vendor's VID. To obtain a dedicated PID, follow Espressif's process at
<https://github.com/espressif/usb-pids> (open a pull request adding the product).
VID/PID will be configurable via Kconfig when the USB component lands (M2).
