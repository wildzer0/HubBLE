# HubBLE — Agent progress

> Resume protocol: in any new session, read `CLAUDE.md` first, then this file, and continue from "Next".

## Current milestone: M0 — Project bootstrap (target `v0.1.0`)

### Decisions (confirmed by the user)
- Repo: `wildzer0/HubBLE`, **public**, license **MIT**.
- ESP-IDF pinned to **v5.5.6** (latest stable 5.x, verified online 2026-11-11).

### Done
- Read CLAUDE.md in full.
- Environment check: git 2.53, gh 2.46 (auth `wildzer0`), node 22.23, npm 10.9, python 3.14.4,
  cmake, ninja, gcc, clang-format present. Docker and ESP-IDF missing.
- Git repo initialised on `main`.

### Next
- Create GitHub repo, push initial commit, branch `milestone/m0-bootstrap`.
- Install ESP-IDF v5.5.6 in the user home only (`~/esp/`), if Python 3.14 allows it.
- Sub-steps 5.1 → 5.7, then release.

### Open issues
- No Docker locally; local ESP-IDF install may be blocked by Python 3.14 compatibility.
