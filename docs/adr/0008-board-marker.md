# ADR 8: Board marker inside the firmware image

Status: accepted

## Context

OTA must refuse an image built for another board (different LED/GPIO constants, partition table), but the ESP-IDF app descriptor has no board field and the device cannot buffer a whole image in RAM.

## Decision

Embed the string `HUBBLE_BOARD_ID=<id>;` in every image (board component) and scan the incoming byte stream for it with a small streaming state machine (host-tested). Refuse the image if the marker is missing or differs. M6 additionally carries the board id in the signed manifest.

## Consequences

No custom app-descriptor tooling; works for any upload path. The marker is not authenticated, so it protects against mistakes, not attackers (signatures arrive in M6).
