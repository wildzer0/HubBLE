# ADR 6: Signed OTA images

Status: accepted (revised in M6)

## Context

OTA images must be authentic; the signing key lives only in a CI secret. CLAUDE.md asked for ECDSA "signed app images
without hardware secure boot (verify the exact ESP-IDF option)".

## Findings (ESP-IDF 5.5.6)

- ESP32-S3 supports **only RSA (Secure Boot V2, RSA-3072/PSS)** for signed apps. ECDSA signing (`SECURE_SIGNED_APPS_ECDSA_SCHEME`
  / `ECDSA_V2`) is available on other chips (C2, C6, H2, P4...), not on the S3 (`SOC_SECURE_BOOT_V2_RSA` only).
- `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` with `CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME` and
  `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` makes the build sign `hubble.bin` and makes `esp_ota_end()` verify the
  signature of every written image, without burning eFuses.

## Decision

Use **RSA-3072 signed app images without hardware secure boot** (user approved the deviation from ECDSA, 2026-11-12).
Development builds use a git-ignored development key; the release key is generated with the user before the first
release, backed up offline and stored as the `HUBBLE_SIGNING_KEY` Actions secret (`docs/key-management.md`).
The release workflow fails without the secret and `tools/make_release_assets.py` re-verifies each image with `espsecure`.

## Consequences

- Key loss means a USB re-flash for every device; key generation requires explicit user approval.
- Not a defence against physical access (no hardware secure boot); documented in `docs/security.md`.
- The SHA-256 in `manifest.json` protects the download; the signature protects authenticity. Both are checked.
