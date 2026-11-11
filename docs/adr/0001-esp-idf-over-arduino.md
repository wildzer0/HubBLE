# ADR 1: ESP-IDF instead of Arduino

Status: accepted

## Context

The dongle needs fine control of TinyUSB, NimBLE, OTA rollback and partitions.

## Decision

Use ESP-IDF (C17), pinned to v5.5.6.

## Consequences

Full access to IDF features and Kconfig; steeper learning curve; CI uses the official espressif/idf image.
