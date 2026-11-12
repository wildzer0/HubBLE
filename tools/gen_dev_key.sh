#!/usr/bin/env bash
# Create the DEVELOPMENT signing key used by local builds: tools/gen_dev_key.sh [path]
# RSA-3072 (the only scheme the ESP32-S3 supports for signed apps). The file is git-ignored (*.pem). Images signed with
# it are accepted only by firmware built with the same key. NEVER use it for releases: the release key is generated
# separately, backed up offline and stored as a CI secret (docs/key-management.md).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
key="${1:-$root/firmware/keys/dev_signing_key.pem}"
if [ -e "$key" ]; then
    echo "exists: $key"
    exit 0
fi
mkdir -p "$(dirname "$key")"
# shellcheck disable=SC1091
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null
espsecure.py generate_signing_key --version 2 --scheme rsa3072 "$key"
echo "created: $key"
