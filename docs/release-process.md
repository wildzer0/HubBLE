# Release process

1. Update `CHANGELOG.md`: move `[Unreleased]` entries under `## [X.Y.Z] - YYYY-MM-DD`.
2. Squash-merge the milestone PR into `main` (CI green).
3. Tag `vX.Y.Z` on `main` and push the tag.
4. `release.yml` rebuilds everything, checks tag vs. changelog, builds the assets
   (`*-ota.bin`, `*-factory.bin`, `manifest.json`, `SHA256SUMS`), publishes the
   GitHub release (pre-release for `v0.*` and `-rc.N`) and deploys the docs.

Signing of OTA images is **stubbed** until M6: until then `*-ota.bin` is the
plain app image. Never move or delete a published tag; fix forward.
