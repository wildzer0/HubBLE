# ADR 5: Dual-OTA partition layout

Status: accepted

## Context

OTA needs two app slots plus rollback and a core dump area.

## Decision

4 MB: two 0x1E0000 slots, 64 KB NVS, core dump and a spare 64 KB. 8 MB: two 3 MB slots with the same logic (see firmware/partitions/).

## Consequences

Max app size 1.875 MB (budget 1.70 MB); no SPIFFS/FATFS partition.
