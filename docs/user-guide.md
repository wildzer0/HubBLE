# User guide

The screenshots come from the web app running against its built-in mock API (`npm run screenshots`), so the values are
examples. The real dongle shows your devices.

## 1. First run: the setup wizard

Join the dongle's Wi-Fi (`HubBLE-XXXX`, default password `hubble-setup`). The web app opens by itself, or browse to
`http://hubble.local`. Until you choose a new password, every page redirects to the wizard.

![Setup wizard, step 1](img/setup-password.png)

Choose a Wi-Fi password of at least 10 characters. The Wi-Fi restarts, so the phone has to rejoin with the new password.
Step 2 shows a **printable QR sticker**: the first code joins the Wi-Fi, the second opens the web app. Print it and stick
it on the dongle (the password is only in the code, not written anywhere else).

![QR sticker](img/setup-sticker.png)

## 2. Devices

The app has four tabs along the bottom: **Devices**, **Update**, **Settings** and **System**. The Devices tab shows one card per paired device (tap it for the details): type, connection state, battery, signal, the **security badge** (see below) and the measured
latency the dongle adds. The top shows the firmware version, the USB state seen by the computer and when the Wi-Fi switches off.

The **Test input** button opens the input tester and **Add device** starts a pairing.

![Devices](img/dashboard.png)

| Badge | Meaning |
|---|---|
| 🟢 Verified (passkey) | Keyboard paired with a passkey typed on it: protected against a man in the middle. |
| 🟡 Verified (challenge) | Just Works device checked by a challenge: you know *which* device it is, but a radio attacker during the pairing window is not excluded. |
| 🔴 Legacy pairing | Old pairing without LE Secure Connections, allowed by you for one device. Can be eavesdropped. |

## 3. Adding a device

**Add device**, put the keyboard or mouse in pairing mode, **Scan for devices**, pick it. Pairing lasts at most 60 seconds
and one device at a time.

![Scan list](img/pair-scan.png)

**Keyboards** show a 6-digit passkey: type it on the keyboard and press Enter.

![Passkey](img/pair-passkey.png)

**Mice, trackballs and keyboards that only do Just Works** go into *quarantine*: they are connected but muted (nothing
reaches the computer). Press the buttons in the order shown (or type the code) within 30 seconds. Progress is live. A wrong
answer or a timeout removes the device again.

![Challenge](img/pair-challenge.png)

If *Require physical confirmation* is on and a keyboard is already paired, finish by holding **both Shift keys for 3
seconds** on that keyboard. Three failed pairings in a row lock pairing for 60 seconds. Details and limits:
[Pairing and security](pairing-and-security.md).

## 4. Device details

Rename or forget a device, see the negotiated connection interval, signal, battery, reconnects and latency statistics
(minimum, average, 95th percentile, maximum), and a battery chart of the current session. **Export report map** downloads
the device's HID report map as hex (useful for bug reports; the device must be connected).

![Device details](img/device-detail.png)

## 5. Input tester

Open it from **Test input** on the Devices tab. Press **Start** to see, live, what each device sends: keys with modifiers, mouse movement, wheel and buttons. It only runs
while the page is open and enabled; nothing is stored. Quarantined devices are not shown.

![Input tester](img/tester.png)

## 6. Settings

Physical confirmation, how long the Wi-Fi stays on (after power-up and after the last client), remote wakeup, LED
brightness and indication, the Shift gesture time, the update channel, and the Wi-Fi network used for updates. The network
password is write-only: the dongle never shows it again. Language (English or Italian) is here too.

![Settings](img/settings.png)

To choose the **Wi-Fi network for updates**, tap **Search networks**: the dongle lists the networks around it (strongest
first, with a padlock for protected ones), you tap yours and type the password. Use **Other network…** only for a hidden
network. While it searches, this connection may stall for a second or two.

![Choosing a Wi-Fi network](img/settings-wifi.png)

## 7. Updates

**Check for updates** joins your saved Wi-Fi network briefly, finds the newest release of the chosen channel (*Stable* or
*Pre-release*) and shows its version, size, signature state and release notes. **Install** downloads it, verifies the SHA-256
and the signature and restarts. HID forwarding keeps working during the download. If physical confirmation is on, hold both
Shifts for 3 seconds when asked. An older version needs an explicit confirmation. Or install a `*-ota.bin` file you
downloaded. If the new firmware does not pass its self-test it is rolled back automatically. See
[OTA and recovery](ota-and-recovery.md).

![Update](img/update.png)

## 8. System

Uptime, last reset reason, free memory, power state, Bluetooth and Wi-Fi status, live log with download, **backup and
restore** of the settings (paired devices are never exported, they contain keys), reboot, **reboot to USB download mode**
(re-flash without touching BOOT) and **factory reset** (erases everything, including all pairings).

![System](img/system.png)

## 9. How the dongle behaves

- The Wi-Fi is **off** during normal use. It turns on at power-up for a few minutes, while no device is paired, and when you
  hold **both Shift keys for 3 seconds** on a paired keyboard.
- While at least one phone has the web app open, the Wi-Fi stays on; after the last one leaves it switches off after the
  idle time.
- When the computer **sleeps**, the dongle asks your devices to save battery, turns the Wi-Fi and LED off, and (if allowed)
  lets a key press wake the computer. See [Power](power.md).
- **Forgot the Wi-Fi password?** Hold both Shifts and Esc for 10 seconds on a paired keyboard. See
  [Troubleshooting](troubleshooting.md).

## 10. LED

Off when a device is connected. Slow amber pulse: no device. Blue breathing: Wi-Fi on. Cyan blink: pairing. Magenta blink:
quarantine challenge. Purple blink: update. Three red blinks at start: a rollback happened. The LED is always off while the
computer sleeps; brightness is configurable (0 turns it off).
