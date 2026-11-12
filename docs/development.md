# Development

## Prerequisites

- ESP-IDF **v5.5.6** (pinned; CI uses `espressif/idf:v5.5.6`)
- Node.js 22, Python 3, CMake, Ninja, clang-format

## Web app

```bash
cd webapp
npm ci
npm run dev        # dev server with the mock API
npm run typecheck && npm run lint && npm test
npm run build && npm run size
npm run e2e        # Playwright (mobile viewport) against the mock API
```

## Firmware

```bash
. $IDF_PATH/export.sh
tools/build-firmware.sh all      # builds s3_zero (xiao_s3 is paused but can still be named); builds into firmware/build-<board>/
python3 tools/check_firmware_size.py firmware/build-s3_zero/hubble.bin 1782579
```

Test overlays (never for releases) build into `build-<board>-<overlay>/`:

```bash
HUBBLE_VERSION=0.2.1-rc.2 HUBBLE_OVERLAY=rollback_test tools/build-firmware.sh s3_zero   # self-test always fails
HUBBLE_OVERLAY=ap_timeout_test tools/build-firmware.sh s3_zero                           # pretend a device is bonded
```

From M2 there is no serial console: TinyUSB owns the native USB port (ADR 10). Read logs in the web app (System -> Live log).
For USB input tests without BLE build with `HUBBLE_OVERLAY=debug_usb` and use `POST /api/v1/debug/input`.
Run `tools/check.sh quick|full` before every commit (local mirror of CI).

## Host unit tests

```bash
cmake -S firmware/test -B firmware/test/build -G Ninja
cmake --build firmware/test/build && ctest --test-dir firmware/test/build --output-on-failure
```

## Docs

```bash
pip install -r docs/requirements.txt
mkdocs serve
doxygen Doxyfile                 # firmware API docs -> docs/api/generated/firmware
(cd webapp && npm run docs)      # TypeDoc -> docs/api/generated/webapp
```

## Local signing

Firmware builds are signed (RSA-3072, ADR 6). `tools/build-firmware.sh` creates a git-ignored development key in
`firmware/keys/` on first use; set `HUBBLE_SIGNING_KEY=<pem>` to sign with another key. Releases use the key from the
`HUBBLE_SIGNING_KEY` Actions secret. See `key-management.md`. Images signed with different keys do not accept each other.

## Sanitizers and fuzzing

```bash
# All host tests under AddressSanitizer + LeakSanitizer + UBSan
cmake -S firmware/test -B firmware/test/build-asan -G Ninja -DHUBBLE_SANITIZE=ON
cmake --build firmware/test/build-asan && ctest --test-dir firmware/test/build-asan --output-on-failure

# Fuzz targets (hid_parser, dns_reply, ota_manifest) for 60 s each; CC=clang gives real libFuzzer
tools/fuzz.sh 60
```

Targets live in `firmware/test/fuzz/` with seed corpora in `firmware/test/fuzz/corpus/<target>/`. A finding leaves a
`crash-*` file in `firmware/test/build-fuzz/` (a `crash-last-input.bin` with the fallback driver): add it as a regression test.
`tools/check.sh full` runs the sanitized tests and a 10 s fuzz run per target.

## Screenshots for the user guide

`cd webapp && npm run screenshots` regenerates `docs/img/*.png` from the mock API (mobile viewport).
