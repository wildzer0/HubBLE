/**
 * @file board_marker.h
 * @brief Streaming scanner that finds the board marker inside a firmware image (pure C, host-testable).
 *
 * Every image embeds the string `HUBBLE_BOARD_ID=<id>;` (see board.c). While an
 * uploaded image streams to flash, the OTA code feeds each chunk to this scanner,
 * then refuses images built for another board (CLAUDE.md §3.8) without ever
 * buffering the whole image in RAM.
 * Dependencies: libc only. Threading: one scanner per upload, not shared.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Literal that precedes the board id. */
#define BOARD_MARKER_PREFIX "HUBBLE_BOARD_ID="
/** Maximum board id length including NUL. */
#define BOARD_MARKER_ID_MAX 16

/** Scanner state; initialise with board_marker_init(). */
typedef struct {
    size_t prefix_pos;            /**< How many prefix bytes have matched so far. */
    bool reading_id;              /**< true while collecting id characters after the prefix. */
    char id[BOARD_MARKER_ID_MAX]; /**< Id collected so far / result. */
    size_t id_len;                /**< Length of id. */
    bool found;                   /**< true once a complete, valid marker was seen. */
} board_marker_scanner_t;

/**
 * @brief Reset a scanner.
 *
 * @param s Scanner.
 */
void board_marker_init(board_marker_scanner_t *s);

/**
 * @brief Feed the next chunk of the image.
 *
 * Chunk boundaries may fall anywhere, including inside the marker. The first
 * valid marker wins; later data is ignored.
 *
 * @param s    Scanner.
 * @param data Chunk bytes.
 * @param len  Chunk length.
 */
void board_marker_feed(board_marker_scanner_t *s, const uint8_t *data, size_t len);

/**
 * @brief Get the board id found in the stream.
 *
 * @param s Scanner.
 * @return The NUL-terminated id, or NULL if no valid marker was seen.
 */
const char *board_marker_id(const board_marker_scanner_t *s);

#ifdef __cplusplus
}
#endif
