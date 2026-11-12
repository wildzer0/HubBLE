# ADR 10: USB HID layout and console removal

Status: accepted

## Context

CLAUDE.md §3.2 requires a boot-compatible keyboard (BIOS/UEFI), consumer control, a 16-bit mouse with pan and an
optional system control. BIOS/UEFI only understand the fixed boot keyboard report, which has no report ID. TinyUSB
takes the S3's single USB PHY, which also hosts the USB Serial/JTAG console used until M1 (ADR 9).

## Decision

- Two HID interfaces: **interface 0** is a boot-protocol keyboard (subclass 1, protocol 1, no report IDs, 8-byte
  report, 1-byte LED output); **interface 1** carries mouse (ID 1), consumer control (ID 2) and system control (ID 3).
  A single interface with report IDs would break boot-protocol keyboards.
- System control exposes Sleep and Wake Up only (no Power Down, to avoid accidental shutdowns).
- The merge logic and descriptors live in the pure `hid_report` component (host-tested); `usb_hid` owns TinyUSB.
- The USB console is removed (`CONFIG_ESP_CONSOLE_NONE`); logs are available in the web app's log view. No CDC
  interface in release builds. A debug CDC variant stays possible later.
- A test overlay (`debug_usb`) enables `POST /api/v1/debug/input` to inject input without BLE.

## Consequences

- Serial flashing with an automatic reset no longer works once the firmware runs; use **System -> Reboot to USB
  download mode** (the firmware releases the PHY first), BOOT+plug, or OTA.
- Keyboard rollover is 6KRO with the phantom-state error report when exceeded (NKRO is a stretch goal).
