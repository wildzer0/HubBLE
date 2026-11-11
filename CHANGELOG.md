# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-11-11

First pre-release: project bootstrap. No functional features yet.

### Added

- Repository layout, license (MIT), contributing/security docs, templates, Dependabot.
- ESP-IDF v5.5.6 firmware skeleton for ESP32-S3-Zero (4 MB) and XIAO ESP32-S3 (8 MB):
  Kconfig board selection, `board` and `led` components, boot banner with version,
  dual-OTA partition tables with rollback enabled, `semver` and `web_assets` components.
- Host unit-test harness (Unity + CMake).
- Web app skeleton (Vite, Preact, strict TypeScript) with mock API, EN/IT i18n stub,
  Vitest, Playwright smoke tests and a gzip size check; embedded into the firmware image.
- Documentation site (MkDocs Material), ADRs, Doxygen and TypeDoc configuration.
- GitHub Actions CI and tag-triggered release pipeline (image signing stubbed until M6).
