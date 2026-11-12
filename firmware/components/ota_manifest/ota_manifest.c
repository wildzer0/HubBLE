/**
 * @file ota_manifest.c
 * @brief Release manifest parsing and install decision. See ota_manifest.h.
 *
 * Dependencies: semver, cJSON. Threading: stateless.
 */
#include "ota_manifest.h"

#include <ctype.h>
#include <string.h>

#include "cJSON.h"
#include "semver.h"

/** Key whose string value is the release tag in the GitHub Releases API. */
#define TAG_KEY "\"tag_name\""
/** Image sizes outside this range cannot be a HubBLE OTA image. */
#define IMAGE_MIN_BYTES 1024u
#define IMAGE_MAX_BYTES (16u * 1024u * 1024u)

/**
 * @brief Whether a character may appear in a release tag.
 *
 * @param c Character.
 * @return true for letters, digits and `. - + _`.
 */
static bool tag_char_ok(char c) {
    return isalnum((unsigned char)c) || c == '.' || c == '-' || c == '+' || c == '_';
}

/** @copydoc ota_manifest_find_tag */
bool ota_manifest_find_tag(const char *json, size_t len, char *out, size_t cap) {
    if (json == NULL || out == NULL || cap == 0) {
        return false;
    }
    const size_t klen = sizeof(TAG_KEY) - 1;
    for (size_t i = 0; i + klen < len; i++) {
        if (memcmp(json + i, TAG_KEY, klen) != 0) {
            continue;
        }
        size_t p = i + klen;
        while (p < len && (json[p] == ' ' || json[p] == ':')) {
            p++;
        }
        if (p >= len || json[p] != '"') {
            return false;
        }
        p++;
        size_t n = 0;
        while (p < len && json[p] != '"' && n + 1 < cap && tag_char_ok(json[p])) {
            out[n++] = json[p++];
        }
        if (p >= len || json[p] != '"' || n == 0) {
            out[0] = '\0';
            return false; /* Truncated, too long or containing odd characters. */
        }
        out[n] = '\0';
        return true;
    }
    return false;
}

/** @copydoc ota_sha256_hex_valid */
bool ota_sha256_hex_valid(const char *s) {
    if (s == NULL || strlen(s) != OTA_SHA256_HEX_LEN) {
        return false;
    }
    for (size_t i = 0; i < OTA_SHA256_HEX_LEN; i++) {
        if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Copy a JSON string field into a fixed buffer, rejecting missing or oversized values.
 *
 * @param o   Object.
 * @param key Field name.
 * @param dst Destination.
 * @param cap Capacity of @p dst.
 * @return true if the field is a string that fits.
 */
static bool copy_str(const cJSON *o, const char *key, char *dst, size_t cap) {
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsString(f) || f->valuestring == NULL || strlen(f->valuestring) >= cap) {
        return false;
    }
    strcpy(dst, f->valuestring);
    return true;
}

/**
 * @brief Whether an asset file name is a plain file name (no path tricks), as it is appended to a download URL.
 *
 * @param s Name.
 * @return true if non-empty and made of letters, digits and `. - _ +` only.
 */
static bool file_name_ok(const char *s) {
    if (s[0] == '\0' || s[0] == '.') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (!tag_char_ok(*s)) {
            return false;
        }
    }
    return true;
}

/** @copydoc ota_manifest_parse */
ota_manifest_result_t ota_manifest_parse(const char *json, const char *board_id, ota_release_t *out) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return OTA_MANIFEST_BAD_JSON;
    }
    ota_release_t r;
    memset(&r, 0, sizeof(r));
    semver_t probe;
    bool ok = copy_str(root, "version", r.version, sizeof(r.version)) && semver_parse(r.version, &probe) &&
              copy_str(root, "min_version", r.min_version, sizeof(r.min_version)) &&
              semver_parse(r.min_version, &probe);
    if (!ok) {
        cJSON_Delete(root);
        return OTA_MANIFEST_BAD_JSON;
    }
    copy_str(root, "release_notes_url", r.notes_url, sizeof(r.notes_url)); /* Optional. */
    const cJSON *sg = cJSON_GetObjectItemCaseSensitive(root, "signed");
    r.signed_image = cJSON_IsTrue(sg);

    const cJSON *boards = cJSON_GetObjectItemCaseSensitive(root, "boards");
    const cJSON *b = cJSON_IsObject(boards) ? cJSON_GetObjectItemCaseSensitive(boards, board_id) : NULL;
    const cJSON *ota = cJSON_IsObject(b) ? cJSON_GetObjectItemCaseSensitive(b, "ota") : NULL;
    if (!cJSON_IsObject(ota)) {
        cJSON_Delete(root);
        return OTA_MANIFEST_NO_BOARD;
    }
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(ota, "size");
    ok = copy_str(ota, "file", r.file, sizeof(r.file)) && file_name_ok(r.file) &&
         copy_str(ota, "sha256", r.sha256, sizeof(r.sha256)) && ota_sha256_hex_valid(r.sha256) &&
         cJSON_IsNumber(size) && size->valuedouble >= IMAGE_MIN_BYTES && size->valuedouble <= IMAGE_MAX_BYTES;
    if (ok) {
        r.size = (uint32_t)size->valuedouble;
    }
    cJSON_Delete(root);
    if (!ok) {
        return OTA_MANIFEST_BAD_ASSET;
    }
    *out = r;
    return OTA_MANIFEST_OK;
}

/** @copydoc ota_manifest_decide */
ota_decision_t ota_manifest_decide(const char *running, const ota_release_t *rel, bool require_signed) {
    semver_t run;
    semver_t next;
    semver_t min;
    if (!semver_parse(running, &run) || !semver_parse(rel->version, &next) || !semver_parse(rel->min_version, &min)) {
        return OTA_DECISION_BAD_VERSION;
    }
    if (require_signed && !rel->signed_image) {
        return OTA_DECISION_UNSIGNED;
    }
    const int c = semver_compare(&next, &run);
    if (c == 0) {
        return OTA_DECISION_UP_TO_DATE;
    }
    if (c < 0) {
        return OTA_DECISION_DOWNGRADE;
    }
    if (semver_compare(&run, &min) < 0) {
        return OTA_DECISION_TOO_OLD;
    }
    return OTA_DECISION_INSTALL;
}
