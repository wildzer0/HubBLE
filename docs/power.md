# Power

The dongle follows the host: when the computer sleeps, the peripherals are asked to save battery, and when it wakes the
full speed comes back at once. Pure logic lives in `power_policy` (host-tested); `bridge` applies it.

## What happens

| Event | Dongle behaviour |
|---|---|
| Host suspends the USB bus | After **2 s** of continuous suspend (`CONFIG_HUBBLE_POWER_SUSPEND_DELAY_MS`, filters short idle suspends) the dongle enters **low power**: it asks every peripheral for a **40 ms** connection interval with **peripheral latency 10** (`CONFIG_HUBBLE_POWER_LOW_*`; the supervision timeout is derived automatically, at least 4 s), switches the **Wi-Fi AP off** (unless an update is installing) and turns the **LED off**. |
| A peripheral sends input while the host sleeps | If *Wake the computer from sleep* is on (default) **and** the host enabled remote wakeup for this suspend, the dongle signals USB remote wakeup (at most once per second). The waking event is **not** forwarded; the host resynchronises its keyboard and mouse state when it resumes. With latency 10 at 40 ms the dongle may notice a key press up to about 0.4 s late. |
| Host resumes | Immediately back to the **7.5 ms** profile (no debounce), AP policy and LED resume. A boot window that had not expired is still there. |

Not done on purpose: the ESP32-S3 itself does not enter light sleep. Doing so would drop the USB link and the BLE
connections the dongle must keep alive to wake the host, so its own consumption is only reduced by switching the radio
(AP) and the LED off.

## Settings (Settings page, Power and LED)

| Setting | Default | Effect |
|---|---|---|
| Wake the computer from sleep | on | Allows remote wakeup. Applies immediately. |
| LED brightness | 25 % | 0 turns the LED off completely. Applies within a second. |
| Dim green when a device is connected | off | Optional "all good" indication. |
| Hold Ctrl and Alt for | 3 s | Time of the Wi-Fi-on gesture, 1 to 10 s. Applies after the next restart. |

## LED patterns

| State | Pattern |
|---|---|
| Host suspended, or brightness 0 | off |
| Update installing | purple, fast blink |
| Quarantine challenge | magenta blink |
| Scanning, pairing, waiting for confirmation | cyan, fast blink |
| Wi-Fi AP on | blue, breathing |
| No device connected | amber, slow pulse |
| Device connected | off (dim green if enabled) |

A rollback is still shown at boot as three red blinks.

## Limitations

- Some PCs cut USB power in sleep or hibernate, or disable USB wake-up in the BIOS or by IT policy. Then the dongle is
  simply off while the computer sleeps and reconnects its peripherals when power returns; nothing is lost but the wake-up
  from the keyboard.
- Remote wakeup only works if the host enabled it for the device. The System page shows the power state; the log says
  when the host allowed or refused wakeup.
- A peripheral may refuse the low-power parameters (some cheap controllers do); it then simply keeps the old interval.
  The connection parameters shown on the device page are the values really negotiated.
- Peripheral latency trades battery against wake-up delay and against the first key press after the host resumes: the
  7.5 ms profile is requested at once, but the peripheral applies it at its next connection event (up to 0.4 s).

## How to measure (hardware needed)

Record the battery current of the B68 and the EM06 (a USB power meter on their charging port, or a bench supply) with
the dongle (a) active, (b) after the computer slept for several minutes, in `docs/testing/m7.md`. Expected: clearly
lower average current in (b). No measurements exist yet: they require the hardware.
