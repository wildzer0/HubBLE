/**
 * @file web_bundle.h
 * @brief Reader for the packed web-app bundle (pure C, host-testable).
 *
 * Bundle format (little-endian), repeated until the end of the blob:
 *   u16 path_len | u32 data_len | path bytes (no NUL) | gzip data
 * It is produced by tools/pack_webapp.py. Every file is already gzipped, so the
 * HTTP server (M1) can send it with `Content-Encoding: gzip` without copying.
 *
 * Dependencies: libc only. Threading: stateless, thread-safe; the blob is read-only.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One file inside the bundle. */
typedef struct {
    const char *path;    /**< Path bytes, NOT NUL-terminated. */
    size_t path_len;     /**< Length of path in bytes. */
    const uint8_t *data; /**< Gzipped file contents (points into the blob). */
    size_t size;         /**< Length of data in bytes. */
} web_bundle_entry_t;

/**
 * @brief Validate the blob and count its entries.
 *
 * @param blob Pointer to the bundle.
 * @param len  Size of the bundle in bytes.
 * @return Number of entries (>= 0), or -1 if the blob is truncated/malformed.
 */
int web_bundle_count(const uint8_t *blob, size_t len);

/**
 * @brief Look up a file by exact path.
 *
 * @param blob  Pointer to the bundle.
 * @param len   Size of the bundle in bytes.
 * @param path  NUL-terminated path to find, e.g. "index.html".
 * @param entry Receives the entry on success.
 * @return 1 if found, 0 if not found, -1 if the blob is malformed.
 */
int web_bundle_find(const uint8_t *blob, size_t len, const char *path, web_bundle_entry_t *entry);

#ifdef __cplusplus
}
#endif
