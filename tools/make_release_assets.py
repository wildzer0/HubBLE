#!/usr/bin/env python3
"""Create the release assets for every built board (CLAUDE.md §6.2).

For each board it writes, into <out-dir>:
  hubble-<board>-v<ver>-ota.bin      app image for OTA (signing is STUBBED until M6: plain app image)
  hubble-<board>-v<ver>-factory.bin  bootloader + partition table + otadata + app merged for the first USB flash
plus manifest.json (version, boards, sizes, SHA-256) and SHA256SUMS.

Usage: make_release_assets.py <version> <firmware-dir> <out-dir> <board> [<board> ...]
Requires an activated ESP-IDF environment (esptool) and builds in <firmware-dir>/build-<board>/.
"""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO_URL = "https://github.com/wildzer0/HubBLE"


def sha256(path: Path) -> str:
    """Return the hex SHA-256 of a file."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def describe(path: Path) -> dict:
    """Manifest entry (file name, size, SHA-256) for an asset."""
    return {"file": path.name, "size": path.stat().st_size, "sha256": sha256(path)}


def main() -> int:
    if len(sys.argv) < 5:
        print(__doc__, file=sys.stderr)
        return 2
    version, fw_dir, out = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    boards = sys.argv[4:]
    out.mkdir(parents=True, exist_ok=True)
    manifest = {
        "version": version,
        "min_version": "0.1.0",
        "release_notes_url": f"{REPO_URL}/releases/tag/v{version}",
        "signed": False,  # flips to true in M6 when image signing is enabled
        "boards": {},
    }
    for board in boards:
        build = fw_dir / f"build-{board}"
        ota = out / f"hubble-{board}-v{version}-ota.bin"
        factory = out / f"hubble-{board}-v{version}-factory.bin"
        shutil.copyfile(build / "hubble.bin", ota)
        # `@flash_args` lists every (offset, file) pair of the build, relative to the build directory.
        subprocess.run(
            ["python3", "-m", "esptool", "--chip", "esp32s3", "merge_bin", "-o", str(factory.resolve()), "@flash_args"],
            cwd=build,
            check=True,
        )
        manifest["boards"][board] = {"ota": describe(ota), "factory": describe(factory)}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    sums = [f"{sha256(p)}  {p.name}" for p in sorted(out.iterdir()) if p.name != "SHA256SUMS"]
    (out / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    print("\n".join(sums))
    return 0


if __name__ == "__main__":
    sys.exit(main())
