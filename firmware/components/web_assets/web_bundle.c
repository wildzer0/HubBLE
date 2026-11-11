/**
 * @file web_bundle.c
 * @brief Bundle reader implementation. See web_bundle.h for the format.
 *
 * Dependencies: libc. Threading: stateless.
 */
#include "web_bundle.h"

#include <stdbool.h>
#include <string.h>

/** Size of the per-entry header: u16 path_len + u32 data_len. */
#define ENTRY_HEADER_SIZE 6U

/**
 * @brief Decode the entry starting at *offset and advance past it.
 *
 * @param blob   Bundle start.
 * @param len    Bundle size.
 * @param offset In/out cursor.
 * @param entry  Receives the decoded entry.
 * @return true if the entry fits entirely inside the blob.
 */
static bool next_entry(const uint8_t *blob, size_t len, size_t *offset, web_bundle_entry_t *entry) {
    size_t off = *offset;
    if (len - off < ENTRY_HEADER_SIZE) {
        return false;
    }
    const size_t path_len = (size_t)blob[off] | ((size_t)blob[off + 1] << 8);
    const size_t data_len = (size_t)blob[off + 2] | ((size_t)blob[off + 3] << 8) | ((size_t)blob[off + 4] << 16) |
                            ((size_t)blob[off + 5] << 24);
    off += ENTRY_HEADER_SIZE;
    /* Subtraction form avoids integer overflow on hostile lengths. */
    if (path_len > len - off || data_len > len - off - path_len) {
        return false;
    }
    entry->path = (const char *)&blob[off];
    entry->path_len = path_len;
    entry->data = &blob[off + path_len];
    entry->size = data_len;
    *offset = off + path_len + data_len;
    return true;
}

/** @copydoc web_bundle_count */
int web_bundle_count(const uint8_t *blob, size_t len) {
    if (blob == NULL) {
        return -1;
    }
    size_t off = 0;
    int n = 0;
    web_bundle_entry_t e;
    while (off < len) {
        if (!next_entry(blob, len, &off, &e)) {
            return -1;
        }
        n++;
    }
    return n;
}

/** @copydoc web_bundle_find */
int web_bundle_find(const uint8_t *blob, size_t len, const char *path, web_bundle_entry_t *entry) {
    if (blob == NULL || path == NULL || entry == NULL) {
        return -1;
    }
    const size_t want = strlen(path);
    size_t off = 0;
    web_bundle_entry_t e;
    while (off < len) {
        if (!next_entry(blob, len, &off, &e)) {
            return -1;
        }
        if (e.path_len == want && memcmp(e.path, path, want) == 0) {
            *entry = e;
            return 1;
        }
    }
    return 0;
}
