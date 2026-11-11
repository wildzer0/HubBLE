#!/usr/bin/env bash
# Build the firmware for one board (or all): tools/build-firmware.sh [s3_zero|xiao_s3|all]
# Requires an activated ESP-IDF environment (. $IDF_PATH/export.sh) and a built
# web app in webapp/dist (npm run build in webapp/).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
boards=("${1:-all}")
[ "${boards[0]}" = all ] && boards=(s3_zero xiao_s3)

# Pack the web app so the firmware can embed it.
python3 "$root/tools/pack_webapp.py" "$root/webapp/dist" \
    "$root/firmware/components/web_assets/generated/webapp.bundle.bin"

for b in "${boards[@]}"; do
    echo "== Building $b =="
    idf.py -C "$root/firmware" -B "$root/firmware/build-$b" -DHUBBLE_BOARD="$b" \
        ${HUBBLE_VERSION:+-DHUBBLE_VERSION="$HUBBLE_VERSION"} build
done
