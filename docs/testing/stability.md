# Stability and soak test plan (M8)

To be run on hardware before tagging 1.0.0 (the board was not available during development). Record results in the
tables at the end and attach logs to the release notes.

Setup: B68 + EM06 paired, dongle on a desktop PC, web app open on a phone for the logging runs (System page, live log),
a second PC or a USB analyzer is optional. The build is the release candidate, signed.

## S1. 24 h idle and typing soak

1. Leave everything connected for 24 h, the PC awake. Type a paragraph and move the trackball every hour (a script that
   types through the dongle is ideal; with the `debug_usb` overlay `POST /api/v1/debug/input` can inject input).
2. Pass criteria:
   - no reboot or rollback (System: uptime 24 h, boot failures 0, reset reason unchanged);
   - both devices still connected, `reconnects` low and explained;
   - free internal heap and its minimum (`min_free`) do not trend down: compare the first and the last hour;
   - latency p95 on the device page is stable (see `docs/latency.md`); no stuck keys, no pointer jumps.

## S2. Reconnect storm

1. Power-cycle the B68 and the EM06 alternately every 20 s for 30 minutes (switch off/on, or leave radio range).
2. Pass criteria: each device reconnects every time (the dongle reconnects without the web app), no duplicate entries,
   no stuck keys on the host after a disconnect, heap stable, `reconnects` counts what was done.
3. Variation: move the devices away until the supervision timeout fires, then back.

## S3. OTA under heavy input

1. Start an update (file upload and, once a release exists, from GitHub) while typing continuously and moving the
   trackball (for example holding a key and rolling the ball).
2. Pass criteria: no lost or stuck input during the transfer, the update completes, the new image passes the
   self-test, the old one is the rollback target. Repeat with the Wi-Fi password change and a forced rollback
   (`rollback_test` overlay) to prove the previous image still boots.
3. Negative: interrupt the transfer (unplug the phone, switch the router off during an online update). The dongle keeps
   running the current image and a retry works.

## S4. Wi-Fi coexistence

1. With the AP on and the web app open (WebSocket streaming the input tester), type and move the trackball for 10 minutes.
2. Record the added latency p95 with the AP on and off (device page) and note any dropped events; compare with the
   7.5 ms-interval target (`docs/latency.md`).

## S5. Host sleep cycles

1. Sleep and wake the PC 50 times (a script, or `rtcwake`), waking alternately from the keyboard and from the mouse.
2. Pass criteria: every wake works when remote wakeup is allowed, the waking key is not typed, the 7.5 ms profile returns
   within a second, no stuck keys, the AP and LED come back according to the policy, heap stable.

## S6. Pairing churn

1. Pair and forget both devices 20 times; fail a pairing on purpose (wrong passkey, wrong clicks, timeout) so the lock
   triggers. Pass criteria: no leftover registry entries, no bond without an entry, the lock lasts 60 s and then clears.

## S7. Memory checks

- During S1-S6 note `heap.free` and `heap.min_free` from System every hour (or log `GET /api/v1/diagnostics`
  periodically). A steady decrease of more than a few hundred bytes per hour is a leak: capture the log and the
  core dump if any, and open an issue.
- After a "check for updates" the free heap returns to its previous level (TLS buffers freed).
- The host-side logic is already checked under AddressSanitizer/LeakSanitizer in CI; this covers the firmware.

## Results

| Test | Date | Build | Result | Notes |
|---|---|---|---|---|
| S1 24 h soak | | | | |
| S2 reconnect storm | | | | |
| S3 OTA under input | | | | |
| S4 coexistence | | | | |
| S5 sleep cycles | | | | |
| S6 pairing churn | | | | |
| S7 memory | | | | |
