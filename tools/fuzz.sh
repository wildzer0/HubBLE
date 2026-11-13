#!/usr/bin/env bash
# Run the fuzz targets for a bounded time: tools/fuzz.sh [seconds per target] [target...]
# Targets: hid_parser dns_reply ota_manifest ulink (CLAUDE.md §5.3). Uses libFuzzer when CC is Clang (CI), otherwise the
# small mutation driver in firmware/test/fuzz/fuzz_driver.c. Both build with ASan + UBSan; any finding fails the run.
# A crash input is left next to the binary (crash-* for libFuzzer, crash-last-input.bin for the driver).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
secs="${1:-30}"
shift || true
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(hid_parser dns_reply ota_manifest ulink)
build="$root/firmware/test/build-fuzz"
cmake -S "$root/firmware/test" -B "$build" -G Ninja -DHUBBLE_FUZZ=ON -DHUBBLE_SANITIZE=ON >/dev/null
cmake --build "$build" >/dev/null
cd "$build"
for t in "${targets[@]}"; do
    echo "== fuzz $t (${secs}s)"
    work="$build/work-$t"
    mkdir -p "$work"
    # libFuzzer writes new inputs into the first corpus directory; keep the committed seeds untouched.
    ./fuzz_"$t" -max_total_time="$secs" "$work" "$root/firmware/test/fuzz/corpus/$t" 2>&1 | tail -3
done
echo "== fuzzing finished without findings"
