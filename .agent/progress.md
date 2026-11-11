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
- 5.1 done (hygiene files).
- 5.4, 5.5, 5.6, 5.7 done locally (webapp, embed, docs, CI workflows) — pending: CI run on GitHub, release.
- 5.3 done: host Unity tests (semver, web_bundle) pass: `cmake -S firmware/test -B firmware/test/build && cmake --build firmware/test/build && ctest --test-dir firmware/test/build`.
- 5.2 done: firmware builds for BOTH boards locally (s3_zero app 0x3a4b0 in 1.875 MB slot; xiao_s3 in 3 MB slot).
  Components: board, led, semver (pure, for host tests), web_assets (bundle reader + embed). Rollback enabled.
  Build: `. ~/esp/esp-idf/export.sh; tools/build-firmware.sh [s3_zero|xiao_s3|all]` (needs webapp/dist).

### Next
- **BLOCKED on user decision (tag):** tag `v0.1.0` is pushed but its release run failed at the publish step
  (no `gh` in the IDF container). Fix is merged on main (PR #4). Tags must not be moved/deleted without asking:
  either (A) delete+re-push `v0.1.0` (nothing was published) or (B) fix forward with `v0.1.1`
  (needs a `## [0.1.1]` CHANGELOG section first).
- After the tag run succeeds: verify assets (2x ota, 2x factory, manifest.json, SHA256SUMS), Pages deploy,
  then final report in Italian.

### Environment notes
- ESP-IDF v5.5.6 installed locally at `~/esp/esp-idf` (user-approved, home only); tools in `~/.espressif`.
  Python 3.14 works. Activate with `. ~/esp/esp-idf/export.sh`.

### Open issues
- none
