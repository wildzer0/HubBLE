# Architecture

See the diagram and component list in `CLAUDE.md` §3.1. In M0 only these
components exist:

| Component | Purpose | Host-testable |
|---|---|---|
| `board` | Per-board constants selected via Kconfig | – |
| `led` | WS2812 / GPIO status LED | – |
| `semver` | SemVer parse/compare (future anti-downgrade) | yes |
| `web_assets` | Embedded gzipped web app + bundle reader | reader only |

Pure-logic code never includes ESP-IDF headers so it runs in the Unity host
tests under `firmware/test/`.

## Web app embedding

`tools/pack_webapp.py` gzips every file of `webapp/dist` into one blob
(`u16 path_len | u32 data_len | path | gzip data`). The blob is linked into the
firmware with `EMBED_FILES` and read through `web_bundle.h`. One OTA therefore
updates firmware and web app together.
