#!/usr/bin/env python3
"""Keep docs/api/openapi.yaml honest: it must parse, every `$ref` must resolve, and every route the firmware registers
(`.uri = "...", .method = HTTP_X` in firmware/components) must be documented, and vice versa.

The single wildcard route `/api/v1/devices/*` stands for `/devices/{id}`, `/devices/{id}/report` and
`/devices/{id}/report-map` (GET) or `/devices/{id}` (PATCH, DELETE). Exit status 1 on any problem.
"""
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "docs" / "api" / "openapi.yaml"
ROUTE = re.compile(r'\.uri\s*=\s*"(/api/v1[^"]*)"\s*,\s*\.method\s*=\s*HTTP_(\w+)')
WILDCARD = {
    "GET": ["/api/v1/devices/{id}", "/api/v1/devices/{id}/report", "/api/v1/devices/{id}/report-map"],
    "PATCH": ["/api/v1/devices/{id}"],
    "DELETE": ["/api/v1/devices/{id}"],
}


def refs(node):
    """Yield every `$ref` string found in the document."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "$ref":
                yield v
            else:
                yield from refs(v)
    elif isinstance(node, list):
        for v in node:
            yield from refs(v)


def main() -> int:
    spec = yaml.safe_load(SPEC.read_text())
    problems: list[str] = []

    for r in sorted(set(refs(spec))):
        parts = r.lstrip("#/").split("/")
        node = spec
        for part in parts:
            node = node.get(part) if isinstance(node, dict) else None
        if node is None:
            problems.append(f"unresolved $ref {r}")

    implemented: set[tuple[str, str]] = set()
    for f in sorted((ROOT / "firmware" / "components").rglob("*.c")):
        for uri, method in ROUTE.findall(f.read_text()):
            if uri.endswith("/*"):
                implemented.update((m, p) for p in WILDCARD.get(method, []) for m in [method])
            else:
                implemented.add((method, uri))

    documented = {
        (m.upper(), p)
        for p, ops in spec["paths"].items()
        for m in ops
        if m in ("get", "post", "put", "patch", "delete")
    }
    for method, path in sorted(implemented - documented):
        problems.append(f"{method} {path} is implemented but not documented")
    for method, path in sorted(documented - implemented):
        problems.append(f"{method} {path} is documented but not registered in the firmware")

    for p in problems:
        print(p)
    print(f"{len(implemented)} routes, {len(documented)} documented operations, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
