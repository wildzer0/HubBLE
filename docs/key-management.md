# Signing key management

OTA images are signed (ADR 6). This page is for the maintainer who owns the release key.

## What is signed and what it protects

- Scheme: **RSA-3072, Secure Boot V2 signature block**, appended to every OTA image (the ESP32-S3 supports no other
  signature scheme for app images). Hardware secure boot and flash encryption stay **off**.
- The running firmware verifies the signature of every image it writes (`esp_ota_end`, option
  `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`), whether it comes from the file upload or from GitHub. An unsigned
  or wrongly signed image is refused and never becomes bootable. The trusted key is the one that signed the
  *running* image, so a signed image keeps the chain going: each release must be signed with the same key.
- This stops spoofed or tampered updates (for example a man in the middle of the "check for updates" download, or a
  stranger who learned the Wi-Fi password). It is **not** protection against someone with physical access: without
  hardware secure boot, a USB re-flash can install anything.

## Keys

| Key | Where | Used for |
|---|---|---|
| Development key | `firmware/keys/dev_signing_key.pem` (git-ignored, created by `tools/gen_dev_key.sh`) | local builds and CI test builds |
| Release key | GitHub Actions secret `HUBBLE_SIGNING_KEY` **and** an offline backup | tagged releases (`release.yml`) |

Firmware signed with the development key only accepts updates signed with the development key, and likewise for the
release key. A device flashed with a development build must be re-flashed over USB with a release `factory.bin` to
follow official releases.

## Generating the release key (once, before the first release)

```bash
. ~/esp/esp-idf/export.sh
espsecure.py generate_signing_key --version 2 --scheme rsa3072 hubble-release-key.pem
```

1. Store `hubble-release-key.pem` **offline** (password manager attachment plus an encrypted USB stick). Two copies.
2. Add its full text (including the `BEGIN`/`END` lines) as the repository secret `HUBBLE_SIGNING_KEY`
   (Settings, Secrets and variables, Actions). Then delete the working copy from disk.
3. Never commit it (`*.pem` is git-ignored) and never paste it in issues or chat.

## If the key is lost or leaked

- **Lost:** no new release can be installed over the air on existing devices (they trust only that key). Generate a new
  key and ship a `factory.bin`; users re-flash once over USB with the web installer. There is no remote recovery.
- **Leaked:** treat every device as exposed to malicious updates. Generate a new key, publish a release signed with
  the old key that contains the new key if it is still safe to do so, otherwise require a USB re-flash, and publish a
  security advisory (`SECURITY.md`).

## Migration note

Devices running v0.6.x or older do not verify signatures, so they accept the first signed release (v0.7.0 or later,
signed with the release key). From then on unsigned images are refused.

## Local development

```bash
tools/gen_dev_key.sh         # created automatically by tools/build-firmware.sh when missing
tools/build-firmware.sh s3_zero
HUBBLE_SIGNING_KEY=/path/to/other.pem tools/build-firmware.sh s3_zero   # sign with another key
python3 -m espsecure verify_signature --version 2 --keyfile firmware/keys/dev_signing_key.pem firmware/build-s3_zero/hubble.bin
```
