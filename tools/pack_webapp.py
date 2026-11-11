#!/usr/bin/env python3
"""Pack a built web app directory into the firmware's embeddable bundle.

Every file is gzipped (deterministically, mtime=0) and written as
    u16 path_len | u32 data_len | path | gzip data      (little-endian)
which is the format read by firmware/components/web_assets/web_bundle.c.

Usage: pack_webapp.py <dist-dir> <output.bin>
"""
import gzip
import struct
import sys
from pathlib import Path

MAX_PATH = 0xFFFF


def pack(dist: Path) -> bytes:
    """Return the bundle bytes for all files below ``dist`` (sorted by path)."""
    out = bytearray()
    for f in sorted(p for p in dist.rglob("*") if p.is_file()):
        rel = f.relative_to(dist).as_posix().encode()
        if len(rel) > MAX_PATH:
            raise ValueError(f"path too long: {rel!r}")
        data = gzip.compress(f.read_bytes(), compresslevel=9, mtime=0)
        out += struct.pack("<HI", len(rel), len(data)) + rel + data
    return bytes(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    dist, dest = Path(sys.argv[1]), Path(sys.argv[2])
    if not dist.is_dir():
        print(f"error: {dist} is not a directory", file=sys.stderr)
        return 1
    blob = pack(dist)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(blob)
    print(f"packed {dist} -> {dest} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
