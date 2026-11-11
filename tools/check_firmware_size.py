#!/usr/bin/env python3
"""Fail if a firmware image exceeds the size budget (CLAUDE.md §4.3).

Usage: check_firmware_size.py <app.bin> <max-bytes>
The S3-Zero budget is 1.70 MB = 1782579 bytes (90 % of the 0x1E0000 slot).
"""
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    size, limit = os.path.getsize(sys.argv[1]), int(sys.argv[2])
    print(f"{sys.argv[1]}: {size} bytes (limit {limit}, {100 * size // limit}%)")
    if size > limit:
        print("FAIL: firmware exceeds the size budget", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
