/**
 * @file log_buffer.h
 * @brief Fixed-size RAM ring buffer for log text (pure C, host-testable).
 *
 * Responsibilities: keep the most recent log bytes and let readers fetch what
 * was added since a sequence number, so the web app can stream live logs and
 * download the whole buffer (CLAUDE.md §3.11).
 * Dependencies: libc only.
 * Threading: NOT thread-safe by itself; the ESP-IDF hook (log_hook.c) wraps
 * writes and reads in a critical section.
 * Privacy: callers must never write keystrokes or report payloads (CLAUDE.md §3.12).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Ring buffer state. Initialise with log_buffer_init(). */
typedef struct {
    char *data;     /**< Backing storage, owned by the caller. */
    size_t cap;     /**< Capacity of data in bytes. */
    uint64_t total; /**< Total bytes ever written (monotonic sequence number of the next byte). */
} log_buffer_t;

/**
 * @brief Initialise a ring buffer over caller-provided storage.
 *
 * @param lb   Buffer to initialise.
 * @param data Storage of at least @p cap bytes.
 * @param cap  Capacity in bytes (must be > 0).
 */
void log_buffer_init(log_buffer_t *lb, char *data, size_t cap);

/**
 * @brief Append bytes, overwriting the oldest data when full.
 *
 * @param lb  Buffer.
 * @param src Bytes to append.
 * @param len Number of bytes.
 */
void log_buffer_write(log_buffer_t *lb, const char *src, size_t len);

/**
 * @brief Sequence number of the next byte to be written (== bytes written so far).
 *
 * @param lb Buffer.
 * @return Current end sequence; a reader that wants "only new data" starts here.
 */
uint64_t log_buffer_end(const log_buffer_t *lb);

/**
 * @brief Copy data starting at sequence @p from.
 *
 * If @p from is older than the oldest byte still stored, reading starts at the
 * oldest byte (data was overwritten). If @p whole_lines is true, the copy ends
 * at the last '\n' in range so a partial line is never returned (it is
 * delivered by a later call once its newline arrives).
 *
 * @param lb          Buffer.
 * @param from        Sequence number to start from.
 * @param out         Destination.
 * @param out_cap     Destination capacity in bytes.
 * @param whole_lines Only return complete lines.
 * @param next        Receives the sequence number to pass to the next call.
 * @return Number of bytes copied into @p out (not NUL-terminated).
 */
size_t log_buffer_read(const log_buffer_t *lb, uint64_t from, char *out, size_t out_cap, bool whole_lines,
                       uint64_t *next);

#ifdef __cplusplus
}
#endif
