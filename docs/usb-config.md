# Configuring over USB

The dongle can be configured through its USB cable, with no Wi-Fi at all. The same web app runs from the project's HTTPS
site and talks to the dongle with **WebHID**, a browser feature for USB devices.

**Open the app: <https://wildzer0.github.io/HubBLE/app/>**

## First: turn it on

The USB configuration is **off by default** while it is being validated, because it adds an extra interface to the dongle's USB
identity. Turn on **Settings → Configure over USB** (from the phone over Wi-Fi, or from a computer over Wi-Fi) and **restart
the dongle**; after the restart the computer sees the extra interface. Turn it off the same way to go back to the original two.

## What you need

- A computer with **Chrome or Edge** (Firefox, Safari and phone browsers do not support WebHID).
- The dongle plugged into that computer.
- The page open at the address above (WebHID needs HTTPS, so it does not work from `http://192.168.4.1`).

## Steps

1. Plug the dongle in. It answers the page during its **configuration window**: the first minutes after power-up, or after
   you press the **BOOT** button, hold Ctrl and Alt for 3 s, or hold the left and right mouse buttons for 3 s. While no device
   is paired the window is always open.
2. Open the app and press **Connect the dongle**. The browser lists devices: choose **HubBLE** and press *Connect*.
3. Use the app as usual: pair devices, change settings, check the dongle, update the firmware. The live log and the input
   tester work too.

If the app says the window is closed, press BOOT on the dongle (or replug it) and connect again. While the page is open and
in use, the window stays open.

## Security

The link is a vendor-defined HID interface. Any program on the computer could open it, so the dongle answers only while the
configuration window is open: it is the same window that switches the Wi-Fi on, opened by the same physical actions. A program
cannot open it by itself, and cannot change the settings of a dongle whose window has closed. Firmware images sent over USB
are checked for the release signature exactly like those sent over Wi-Fi.

## On a company computer

Joining the dongle's Wi-Fi is not needed, which helps where Wi-Fi is restricted. But WebHID itself may be disabled by the
organisation's browser policy, and some endpoint-protection tools list vendor-defined HID interfaces. If that is the case,
configure the dongle on a personal computer or with the phone over Wi-Fi first, then plug it into the company computer: it
needs no software and no configuration there.

## Linux

Chrome needs permission to open the raw HID device. With systemd, add a rule and replug the dongle:

```text
# /etc/udev/rules.d/70-hubble.rules
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="4001", TAG+="uaccess"
```

Then run `sudo udevadm control --reload && sudo udevadm trigger`.

## Troubleshooting

- **The dongle is not in the list:** replug it, close other tabs using it, and check the Linux rule above.
- **"The configuration window is closed":** press BOOT on the dongle or replug it.
- **Updates over USB:** the firmware image is sent in small acknowledged pieces, so a full update takes a minute or two; keep
  the page open until it finishes.
