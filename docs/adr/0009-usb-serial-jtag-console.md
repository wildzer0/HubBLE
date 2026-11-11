# ADR 9: Console on USB Serial/JTAG

Status: accepted

## Context

The S3-Zero has no UART bridge chip, so UART logs are unreachable; flashing and logs share the native USB port.

## Decision

Use CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG for M0-M1 logging.

## Consequences

Logs are visible on /dev/ttyACM0 without extra hardware. TinyUSB (M2) takes the PHY, so the console will be revisited (debug-only CDC variant, web log view).
