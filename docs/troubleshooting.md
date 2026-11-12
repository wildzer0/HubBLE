# Troubleshooting

## Flashing

- **No serial port appears / flashing fails:** hold BOOT while plugging in the USB cable, then flash again.
- **Wrong chip:** boards sold as "ESP32-C3-Zero" are not supported.
- **Web installer cannot connect:** use Chrome or Edge, a data cable (not charge-only), and put the board in download mode first.

## Wi-Fi and web app

- **The captive portal page does not open:** browse to `http://hubble.local` or `http://192.168.4.1`. Some phones need the
  mobile data switched off to stay on a Wi-Fi without internet.
- **The Wi-Fi network is not there:** the access point is off during normal use. Hold both Shift keys for 3 seconds on a
  paired keyboard. With no device paired it is always on.
- **Forgotten Wi-Fi password:** hold both Shifts and Esc for 10 s on a paired keyboard (the password returns to the
  default `hubble-setup` and the setup wizard starts again; paired devices are kept). Last resort: hold BOOT while
  plugging in and keep it pressed for 10 s (factory reset). See [OTA and recovery](ota-and-recovery.md).
- **The page says "Offline":** the live connection dropped (for example the Wi-Fi restarted). It reconnects by itself.

## Pairing

- **A device is not listed in the scan:** put it in pairing mode again; some peripherals stop advertising after 30-60 s.
  Only devices that look like HID keyboards or mice are shown.
- **"Legacy pairing was refused":** the device does not support LE Secure Connections. You may allow it for this pairing
  (red badge) if you accept the risk, see [Pairing and security](pairing-and-security.md).
- **Pairing is locked:** three failures lock pairing for 60 s.
- **A device does not reconnect:** peripherals using resolvable private addresses are not supported by the allow-list.
  Check the device page for "last seen" and re-pair.

## Typing and pointing

- **Input is muted:** a device in quarantine (during pairing) never reaches the computer until its challenge is passed.
- **Stuck key:** disconnecting a device releases everything it held. If a key repeats on the host, unplug and replug.
- **High latency:** the device page shows the measured added latency. With the Wi-Fi access point on, Bluetooth and Wi-Fi
  share the radio; the AP switches itself off after a few minutes of inactivity, see [Latency](latency.md).

## Updates

- **Update refused:** the message tells why: wrong board, not signed, older version, or the running firmware has not
  passed its self-test yet (wait a minute).
- **Check for updates cannot join the Wi-Fi:** re-enter the network name and password in Settings. The phone may drop for a
  few seconds while the dongle joins your router.
- **The dongle went back to the old version:** the new one failed its self-test and was rolled back automatically; the
  Update page shows the reason.
