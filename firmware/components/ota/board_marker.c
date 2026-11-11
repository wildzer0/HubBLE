/**
 * @file board_marker.c
 * @brief Board marker scanner. See board_marker.h.
 *
 * Dependencies: libc. Threading: none.
 *
 * Note: the scanner's own prefix literal is also present inside every image
 * (this very code). It is followed by NUL, not by an id and ';', so it fails
 * validation and scanning simply continues to the real marker.
 */
#include "board_marker.h"

#include <string.h>

/** @brief true for characters allowed in a board id: [a-z0-9_]. */
static bool is_id_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/** @copydoc board_marker_init */
void board_marker_init(board_marker_scanner_t *s) {
    memset(s, 0, sizeof(*s));
}

/** @brief Abandon the current candidate and restart matching with byte @p c. */
static void restart_with(board_marker_scanner_t *s, uint8_t c) {
    s->reading_id = false;
    s->id_len = 0;
    s->prefix_pos = (c == (uint8_t)BOARD_MARKER_PREFIX[0]) ? 1 : 0;
}

/** @copydoc board_marker_feed */
void board_marker_feed(board_marker_scanner_t *s, const uint8_t *data, size_t len) {
    const size_t prefix_len = strlen(BOARD_MARKER_PREFIX);
    for (size_t i = 0; i < len && !s->found; i++) {
        const uint8_t c = data[i];
        if (s->reading_id) {
            if (is_id_char(c) && s->id_len < BOARD_MARKER_ID_MAX - 1) {
                s->id[s->id_len++] = (char)c;
            } else if (c == ';' && s->id_len > 0) {
                s->id[s->id_len] = '\0';
                s->found = true;
            } else {
                restart_with(s, c);
            }
        } else if (c == (uint8_t)BOARD_MARKER_PREFIX[s->prefix_pos]) {
            if (++s->prefix_pos == prefix_len) {
                s->reading_id = true;
                s->id_len = 0;
            }
        } else {
            restart_with(s, c);
        }
    }
}

/** @copydoc board_marker_id */
const char *board_marker_id(const board_marker_scanner_t *s) {
    return s->found ? s->id : NULL;
}
