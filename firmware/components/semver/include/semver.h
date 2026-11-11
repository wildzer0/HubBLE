/**
 * @file semver.h
 * @brief Semantic Versioning 2.0.0 parsing and comparison (pure C, host-testable).
 *
 * Responsibilities: parse "[v]MAJOR.MINOR.PATCH[-prerelease]" and order versions
 * (used later for OTA anti-downgrade checks, CLAUDE.md §3.8).
 * Dependencies: C standard library only (no ESP-IDF APIs, so it runs in host tests).
 * Threading: pure functions, thread-safe. Build metadata ("+...") is rejected.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of the pre-release string, including the terminating NUL. */
#define SEMVER_PRERELEASE_MAX 32

/** A parsed semantic version. */
typedef struct {
    uint32_t major;                         /**< Major number. */
    uint32_t minor;                         /**< Minor number. */
    uint32_t patch;                         /**< Patch number. */
    char prerelease[SEMVER_PRERELEASE_MAX]; /**< Pre-release identifiers ("rc.1"), empty for releases. */
} semver_t;

/**
 * @brief Parse a version string.
 *
 * @param text NUL-terminated string such as "v0.2.1-rc.1" or "1.0.0".
 * @param out  Receives the parsed version on success (untouched on failure).
 * @return true if the string is a valid version; false otherwise.
 */
bool semver_parse(const char *text, semver_t *out);

/**
 * @brief Compare two versions following SemVer 2.0.0 precedence rules.
 *
 * @param a First version.
 * @param b Second version.
 * @return Negative if a < b, zero if equal, positive if a > b.
 */
int semver_compare(const semver_t *a, const semver_t *b);

#ifdef __cplusplus
}
#endif
