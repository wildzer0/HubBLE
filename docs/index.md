# HubBLE

HubBLE is a tiny ESP32-S3 USB dongle that connects up to four Bluetooth Low
Energy keyboards and mice at once and presents them to the host PC as one
standard wired USB HID device. The host needs no Bluetooth, drivers or software.
A built-in web app (served over the dongle's own Wi-Fi access point) handles
pairing, settings, diagnostics and over-the-air updates.

!!! note "Project status"
    Pre-release. Milestone M0 (project bootstrap) provides the firmware skeleton,
    web app skeleton, CI and docs. Functional features arrive in later milestones;
    see the roadmap in `CLAUDE.md`.

Start with [Getting started](getting-started.md).
