# ADR 4: Embed the gzipped web app in the firmware image

Status: accepted

## Context

Firmware and web app must never be out of sync, and OTA must update both.

## Decision

Pack gzipped files into one blob linked via EMBED_FILES and serve with Content-Encoding: gzip.

## Consequences

One OTA updates everything; web changes need a firmware build; flash cost counts against the size budget.
