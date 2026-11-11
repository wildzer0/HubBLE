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
- Git repo initialised; public repo `wildzer0/HubBLE` created, `main` pushed, branch `milestone/m0-bootstrap` created.
- 5.1 done (commit `chore: add repository hygiene files and layout`).

### Next
- **BLOCKED on user decision:** local ESP-IDF v5.5.6 install (clone + `install.sh esp32s3` into `~/esp/`)
  was denied by the permission classifier. Needed for 5.2 / 5.5 firmware builds.
- Meanwhile independent steps: 5.3 (host Unity tests), 5.4 (web app), 5.6 (docs), 5.7 (CI).

### Open issues
- No Docker locally; ESP-IDF install needs user approval. Python 3.14 may be incompatible with IDF 5.5 tooling.
