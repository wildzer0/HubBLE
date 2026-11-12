#!/usr/bin/env bash
# Local mirror of the CI pipeline (.github/workflows/ci.yml): tools/check.sh [quick|full]
#   quick (default): format, lint, type-check, unit tests, host C tests, web build + size.
#   full: quick + sanitizer test run + short fuzzing + Playwright e2e + firmware build for both boards + size checks.
# Run it before every commit so that CI on GitHub is only a final confirmation.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${1:-quick}"
cd "$root"

echo "== clang-format =="
find firmware/main firmware/components firmware/test/unit -name '*.[ch]' -print0 | xargs -0 clang-format --dry-run -Werror

echo "== markdownlint =="
npx --yes markdownlint-cli2@0.18.1 "**/*.md" >/dev/null

echo "== host C unit tests =="
cmake -S firmware/test -B firmware/test/build -G Ninja >/dev/null
cmake --build firmware/test/build
ctest --test-dir firmware/test/build --output-on-failure

if [ "$mode" = full ]; then
    echo "== host C tests under ASan/LSan/UBSan + short fuzzing =="
    cmake -S firmware/test -B firmware/test/build-asan -G Ninja -DHUBBLE_SANITIZE=ON >/dev/null
    cmake --build firmware/test/build-asan
    ctest --test-dir firmware/test/build-asan --output-on-failure
    tools/fuzz.sh "${FUZZ_SECONDS:-10}"
fi

echo "== web app =="
(
    cd webapp
    npm run typecheck
    npm run lint
    npm run format:check
    npm run test:coverage
    npm run build
    npm run size
    if [ "$mode" = full ]; then npm run e2e; fi
)

if [ "$mode" = full ]; then
    echo "== firmware (both boards) =="
    # shellcheck disable=SC1091
    . "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null
    tools/build-firmware.sh all
    for b in s3_zero xiao_s3; do
        python3 tools/check_firmware_size.py "firmware/build-$b/hubble.bin" "${FIRMWARE_MAX_BYTES:-1782579}"
    done
fi
echo "== all checks passed ($mode) =="
