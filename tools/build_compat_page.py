#!/usr/bin/env python3
"""Rewrite the device table of docs/compatibility.md from the report service's statistics.

Usage: build_compat_page.py <stats.json | https://.../v1/stats> [page.md]

The table goes between the `<!-- compat-table:start -->` and `<!-- compat-table:end -->` markers. If the statistics
cannot be read or parsed, the page is left untouched and the script still exits 0, so a docs build never fails because
the service is down (a note goes to stderr).
"""
import json
import sys
import urllib.request
from pathlib import Path

START = "<!-- compat-table:start -->"
END = "<!-- compat-table:end -->"
DEFAULT_PAGE = Path(__file__).resolve().parent.parent / "docs" / "compatibility.md"
TIMEOUT_S = 15
MAX_BYTES = 4 * 1024 * 1024


def load(source: str) -> dict:
    """Read the statistics from a file path or an http(s) URL."""
    if source.startswith(("http://", "https://")):
        with urllib.request.urlopen(source, timeout=TIMEOUT_S) as resp:  # noqa: S310 (the URL is operator input)
            return json.loads(resp.read(MAX_BYTES))
    return json.loads(Path(source).read_text(encoding="utf-8"))


def cell(text: object) -> str:
    """Make a value safe inside a Markdown table cell."""
    return str(text).replace("|", "\\|").replace("\n", " ").replace("\r", " ").strip() or "-"


def pct(share: float | None) -> str:
    return "-" if share is None else f"{round(share * 100)} %"


def ms(us: int | None) -> str:
    return "-" if us is None else f"{us / 1000:.2f}"


def kind_of(kinds: dict) -> str:
    """The most reported kind of a device."""
    best = max(("keyboard", "pointer", "unknown"), key=lambda k: kinds.get(k, 0))
    return best if kinds.get(best, 0) else "unknown"


def table(stats: dict) -> str:
    devices = stats.get("devices") or []
    if not devices:
        return "No statistics have been published yet."
    lines = [
        f"{stats.get('total', 0)} reports, {len(devices)} devices.",
        "",
        "| Device | Reports | Kind | Parse OK | Latency avg / p95 (ms) | Firmware |",
        "|---|---|---|---|---|---|",
    ]
    for d in devices:
        lat = d.get("latency_us")
        latency = "-" if not lat else f"{ms(lat['avg']['median'])} / {ms(lat['p95']['median'])}"
        versions = ", ".join(sorted((d.get("firmware_versions") or {}).keys())) or "-"
        lines.append(
            f"| {cell(d.get('label') or d.get('key'))} | {d.get('reports', 0)} | {kind_of(d.get('kinds') or {})} "
            f"| {pct(d.get('parse_ok_share'))} | {latency} | {cell(versions)} |"
        )
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    page = Path(sys.argv[2]) if len(sys.argv) == 3 else DEFAULT_PAGE
    try:
        stats = load(sys.argv[1])
        rendered = table(stats)
    except Exception as exc:  # noqa: BLE001 (any failure must leave the page untouched)
        print(f"compat page left untouched: {exc}", file=sys.stderr)
        return 0
    text = page.read_text(encoding="utf-8")
    if START not in text or END not in text or text.index(START) > text.index(END):
        print("compat page left untouched: markers not found", file=sys.stderr)
        return 0
    head, rest = text.split(START, 1)
    _, tail = rest.split(END, 1)
    page.write_text(f"{head}{START}\n\n{rendered}\n\n{END}{tail}", encoding="utf-8")
    print(f"compat table written to {page}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
