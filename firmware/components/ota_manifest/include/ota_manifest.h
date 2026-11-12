/**
 * @file ota_manifest.h
 * @brief Pure logic of "check for updates": release manifest parsing and the install decision.
 *
 * Responsibilities (CLAUDE.md §3.8):
 *  - find the newest release tag in a (truncated) GitHub Releases API answer;
 *  - parse `manifest.json` and pick the entry for this board;
 *  - decide whether a release may be installed (newer, anti-downgrade, minimum version, signed).
 *
 * Dependencies: semver, cJSON (no ESP-IDF API, so it is unit-tested on the host).
 * Threading: stateless; safe from any task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Capacity of the version strings, including the NUL. */
#define OTA_VERSION_MAX 32
/** Capacity of the release-notes URL, including the NUL. */
#define OTA_URL_MAX 160
/** Capacity of an asset file name, including the NUL. */
#define OTA_FILE_MAX 64
/** Length of a SHA-256 in hex, without the NUL. */
#define OTA_SHA256_HEX_LEN 64
/** Capacity of a release tag ("v0.7.0"), including the NUL. */
#define OTA_TAG_MAX 40

/** What the manifest says about the release, for this board. */
typedef struct {
    char version[OTA_VERSION_MAX];       /**< Release version, no leading "v". */
    char min_version[OTA_VERSION_MAX];   /**< Oldest running version that may install this release directly. */
    char notes_url[OTA_URL_MAX];         /**< Release notes page (may be empty). */
    bool signed_image;                   /**< The OTA image carries an RSA signature block. */
    char file[OTA_FILE_MAX];             /**< Asset name of the OTA image for this board. */
    uint32_t size;                       /**< Image size in bytes. */
    char sha256[OTA_SHA256_HEX_LEN + 1]; /**< Lower-case hex SHA-256 of the OTA image. */
} ota_release_t;

/** Result of parsing. */
typedef enum {
    OTA_MANIFEST_OK = 0,   /**< Parsed. */
    OTA_MANIFEST_BAD_JSON, /**< Not JSON, or the top-level fields are missing/invalid. */
    OTA_MANIFEST_NO_BOARD, /**< The manifest has no image for this board. */
    OTA_MANIFEST_BAD_ASSET /**< The board entry is malformed (name, size or SHA-256). */
} ota_manifest_result_t;

/** What to do with a release. */
typedef enum {
    OTA_DECISION_INSTALL = 0, /**< Newer than the running version: may be installed. */
    OTA_DECISION_UP_TO_DATE,  /**< Same version as the running one. */
    OTA_DECISION_DOWNGRADE,   /**< Older: only with the explicit downgrade confirmation. */
    OTA_DECISION_TOO_OLD,     /**< The running version is older than `min_version`: update in steps. */
    OTA_DECISION_UNSIGNED,    /**< The release is not signed but this firmware requires signed images. */
    OTA_DECISION_BAD_VERSION  /**< A version string is not SemVer. */
} ota_decision_t;

/**
 * @brief Extract the first `"tag_name"` of a GitHub Releases API answer.
 *
 * The answer can be large, so the caller passes only its first bytes: in both `/releases/latest` and
 * `/releases?per_page=1` the first `tag_name` is the newest release's, and it appears within the first kilobytes.
 *
 * @param json Response bytes (need not be NUL terminated, may be truncated).
 * @param len  Number of bytes in @p json.
 * @param out  Receives the tag, NUL terminated.
 * @param cap  Capacity of @p out.
 * @return true if a plausible tag (letters, digits, `.`, `-`, `+`, `_`) was found.
 */
bool ota_manifest_find_tag(const char *json, size_t len, char *out, size_t cap);

/**
 * @brief Parse a release `manifest.json` and select the entry of @p board_id.
 *
 * @param json     NUL-terminated manifest text.
 * @param board_id Board identifier, e.g. "s3_zero".
 * @param[out] out Receives the release (only valid on OTA_MANIFEST_OK).
 * @return Parse result.
 */
ota_manifest_result_t ota_manifest_parse(const char *json, const char *board_id, ota_release_t *out);

/**
 * @brief Decide whether @p rel may be installed on a device running @p running.
 *
 * @param running            Running version string.
 * @param rel                Release to evaluate.
 * @param require_signed     true when this firmware only accepts signed images.
 * @return The decision. A downgrade is reported as OTA_DECISION_DOWNGRADE and the caller applies it only when the
 *         user explicitly confirmed it.
 */
ota_decision_t ota_manifest_decide(const char *running, const ota_release_t *rel, bool require_signed);

/**
 * @brief Whether a string is a valid lower-case hex SHA-256.
 *
 * @param s Candidate.
 * @return true if exactly 64 hex digits.
 */
bool ota_sha256_hex_valid(const char *s);

#ifdef __cplusplus
}
#endif
