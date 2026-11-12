#!/usr/bin/env python3
"""Prepare the ESP Web Tools installer files for the docs site (CLAUDE.md §6.4).

Copies the `*-factory.bin` images of a release into <site>/firmware/ and writes one manifest per board
(<site>/firmware/manifest-<board>.json). The images are served from the docs site, not from GitHub release downloads,
because the browser needs CORS headers that release downloads do not send.

Usage: make_web_installer.py <release-assets-dir> <site-dir>
"""
import json
import re
import shutil
import sys
from pathlib import Path

PATTERN = re.compile(r"^hubble-(?P<board>[a-z0-9_]+)-v(?P<version>[0-9A-Za-z.+-]+)-factory\.bin$")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    assets, site = Path(sys.argv[1]), Path(sys.argv[2])
    out = site / "firmware"
    out.mkdir(parents=True, exist_ok=True)
    found = 0
    for f in sorted(assets.glob("*-factory.bin")):
        m = PATTERN.match(f.name)
        if not m:
            continue
        shutil.copyfile(f, out / f.name)
        manifest = {
            "name": f"HubBLE ({m['board']})",
            "version": m["version"],
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": f.name, "offset": 0}]}],
        }
        (out / f"manifest-{m['board']}.json").write_text(json.dumps(manifest, indent=2) + "\n")
        found += 1
    print(f"web installer: {found} board(s) prepared in {out}")
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
