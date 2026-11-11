# ADR 6: Signed OTA images (planned for M6)

Status: accepted

## Context

OTA images must be authentic; the signing key lives only in a CI secret.

## Decision

Use ECDSA signed app images without hardware secure boot (exact ESP-IDF option to be verified in M6). Signing is stubbed in M0.

## Consequences

Key loss means USB re-flash; key generation requires explicit user approval.
