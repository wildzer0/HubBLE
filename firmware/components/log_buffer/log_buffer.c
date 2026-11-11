/**
 * @file log_buffer.c
 * @brief Ring buffer implementation. See log_buffer.h.
 *
 * Dependencies: libc. Threading: none (see header).
 */
#include "log_buffer.h"

#include <string.h>

/** @copydoc log_buffer_init */
void log_buffer_init(log_buffer_t *lb, char *data, size_t cap) {
    lb->data = data;
    lb->cap = cap;
    lb->total = 0;
}

/** @copydoc log_buffer_write */
void log_buffer_write(log_buffer_t *lb, const char *src, size_t len) {
    if (len >= lb->cap) {
        /* Only the last cap bytes can survive; account for the skipped ones in the sequence number. */
        lb->total += len - lb->cap;
        src += len - lb->cap;
        len = lb->cap;
    }
    size_t pos = (size_t)(lb->total % lb->cap);
    size_t first = lb->cap - pos;
    if (first > len) {
        first = len;
    }
    memcpy(lb->data + pos, src, first);
    memcpy(lb->data, src + first, len - first);
    lb->total += len;
}

/** @copydoc log_buffer_end */
uint64_t log_buffer_end(const log_buffer_t *lb) {
    return lb->total;
}

/** @copydoc log_buffer_read */
size_t log_buffer_read(const log_buffer_t *lb, uint64_t from, char *out, size_t out_cap, bool whole_lines,
                       uint64_t *next) {
    const uint64_t oldest = lb->total > lb->cap ? lb->total - lb->cap : 0;
    if (from < oldest) {
        from = oldest; /* Data was overwritten before the reader got to it. */
    }
    if (from > lb->total) {
        from = lb->total; /* Defensive: a cursor from the future (e.g. after a reboot) */
    }
    uint64_t avail = lb->total - from;
    size_t n = avail < out_cap ? (size_t)avail : out_cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = lb->data[(size_t)((from + i) % lb->cap)];
    }
    if (whole_lines && n > 0) {
        size_t keep = n;
        while (keep > 0 && out[keep - 1] != '\n') {
            keep--;
        }
        if (keep == 0 && n == out_cap) {
            keep = n; /* A single line longer than the output buffer: emit it split rather than stall forever. */
        }
        n = keep;
    }
    *next = from + n;
    return n;
}
