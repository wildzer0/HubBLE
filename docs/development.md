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
tools/build-firmware.sh all      # or s3_zero / xiao_s3; builds into firmware/build-<board>/
python3 tools/check_firmware_size.py firmware/build-s3_zero/hubble.bin 1782579
```

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

Image signing is stubbed until M6; this section will describe local development keys.
