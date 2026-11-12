# Release process

1. Update `CHANGELOG.md`: move `[Unreleased]` entries under `## [X.Y.Z] - YYYY-MM-DD`.
2. Squash-merge the milestone PR into `main` (CI green).
3. Tag `vX.Y.Z` on `main` and push the tag.
4. `release.yml` rebuilds everything, checks tag vs. changelog, builds the assets
   (`*-ota.bin`, `*-factory.bin`, `manifest.json`, `SHA256SUMS`), publishes the
   GitHub release (pre-release for `v0.*` and `-rc.N`) and deploys the docs.

`*-ota.bin` is signed with the release key from the `HUBBLE_SIGNING_KEY` secret; the release fails without it and every
image is re-verified with `espsecure` before publishing (`key-management.md`). Never move or delete a published tag;
fix forward.

## First signed release (v1.0.0) checklist

Everything from M2 to M8 was developed on local branches without pushing (user policy). Do this in order, asking the
maintainer before each outward-facing step:

1. **Generate the release signing key** (`key-management.md`), make the two offline backups, add the secret
   `HUBBLE_SIGNING_KEY`. Without it `release.yml` fails on purpose.
2. Run `tools/check.sh full` and, if possible, `tools/fuzz.sh 3600`; complete the hardware checklists in `docs/testing/`
   that were deferred (M1 rollback and OTA, M2 to M7) and `testing/stability.md`; fill `docs/latency.md` and `docs/power.md`
   with the measured numbers and export the B68/EM06 report maps into `firmware/test/fixtures/`.
3. Rename `[Unreleased]` in `CHANGELOG.md` to `## [1.0.0] - YYYY-MM-DD`.
4. Push the milestone branch, open the PR, wait for CI (including the Clang libFuzzer run), squash-merge.
5. Tag `v1.0.0` on `main` and push the tag. `release.yml` builds both boards with the release key, verifies every image,
   publishes the GitHub release, and redeploys the docs so the [web installer](install.md) offers the new images.
6. Install the release on a dongle running a development build over USB (the first signed release cannot be installed
   over the air onto a dongle that trusts a different key) and run the M6 section C checks against the real release.
