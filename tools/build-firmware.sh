#!/usr/bin/env bash
# Build the firmware for one board (or all): tools/build-firmware.sh [s3_zero|xiao_s3|all]
# Requires an activated ESP-IDF environment (. $IDF_PATH/export.sh) and a built
# web app in webapp/dist (npm run build in webapp/).
#
# Environment:
#   HUBBLE_VERSION  version embedded in the image (default: derived from git)
#   HUBBLE_SIGNING_KEY  PEM key to sign with (release); default: firmware/keys/dev_signing_key.pem (auto-created)
#   HUBBLE_OVERLAY  optional test overlay: rollback_test | ap_timeout_test | debug_usb
#                   (builds into build-<board>-<overlay>/ so release builds are never mixed up)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
boards=("${1:-all}")
[ "${boards[0]}" = all ] && boards=(s3_zero xiao_s3)

# Pack the web app so the firmware can embed it.
# Signing key: HUBBLE_SIGNING_KEY (release) or the git-ignored development key, created on first use.
[ -n "${HUBBLE_SIGNING_KEY:-}" ] || "$root/tools/gen_dev_key.sh" >/dev/null
python3 "$root/tools/pack_webapp.py" "$root/webapp/dist" \
    "$root/firmware/components/web_assets/generated/webapp.bundle.bin"

for b in "${boards[@]}"; do
    dir="build-$b${HUBBLE_OVERLAY:+-$HUBBLE_OVERLAY}"
    echo "== Building $b ${HUBBLE_OVERLAY:+(overlay $HUBBLE_OVERLAY)} =="
    idf.py -C "$root/firmware" -B "$root/firmware/$dir" -DHUBBLE_BOARD="$b" \
        ${HUBBLE_OVERLAY:+-DHUBBLE_OVERLAY="$HUBBLE_OVERLAY"} \
        ${HUBBLE_VERSION:+-DHUBBLE_VERSION="$HUBBLE_VERSION"} \
        ${HUBBLE_SIGNING_KEY:+-DHUBBLE_SIGNING_KEY="$HUBBLE_SIGNING_KEY"} build
done
