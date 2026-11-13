/**
 * @file ulink_proto.c
 * @brief USB configuration link framing and HTTP helpers (see ulink_proto.h).
 *
 * Dependencies: libc only. Threading: none.
 */
#include "ulink_proto.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/** @copydoc ulink_parser_init */
void ulink_parser_init(ulink_parser_t *p) {
    p->have = 0;
    p->errors = 0;
}

/** @copydoc ulink_parser_feed */
void ulink_parser_feed(ulink_parser_t *p, const uint8_t *data, size_t n, ulink_frame_cb cb, void *ctx) {
    for (size_t i = 0; i < n; i++) {
        p->buf[p->have++] = data[i];
        if (p->have < 3) {
            continue;
        }
        const size_t len = (size_t)p->buf[0] | ((size_t)p->buf[1] << 8);
        if (len > ULINK_MAX_PAYLOAD) {
            p->errors++;
            p->have = 0; /* A frame this big cannot be real: drop everything and look for a new header. */
            continue;
        }
        if (p->have == 3 + len) {
            cb(p->buf[2], &p->buf[3], len, ctx);
            p->have = 0;
        }
    }
}

/** @copydoc ulink_parser_feed_report */
void ulink_parser_feed_report(ulink_parser_t *p, const uint8_t *report, size_t len, ulink_frame_cb cb, void *ctx) {
    if (len < 1) {
        return;
    }
    size_t n = report[0];
    if (n > ULINK_REPORT_DATA) {
        n = ULINK_REPORT_DATA;
    }
    if (n > len - 1) {
        n = len - 1;
    }
    ulink_parser_feed(p, &report[1], n, cb, ctx);
}

/** @copydoc ulink_encode_header */
void ulink_encode_header(uint8_t type, size_t len, uint8_t out[3]) {
    out[0] = (uint8_t)(len & 0xFF);
    out[1] = (uint8_t)((len >> 8) & 0xFF);
    out[2] = type;
}

/** @copydoc ulink_pack_report */
void ulink_pack_report(uint8_t out[ULINK_REPORT_SIZE], const uint8_t *data, size_t n) {
    memset(out, 0, ULINK_REPORT_SIZE);
    if (n > ULINK_REPORT_DATA) {
        n = ULINK_REPORT_DATA;
    }
    out[0] = (uint8_t)n;
    if (n > 0) {
        memcpy(&out[1], data, n);
    }
}

/** @brief Case-insensitive prefix test. */
static bool starts_with_ci(const char *s, const char *prefix) {
    while (*prefix != '\0') {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

/** @copydoc ulink_http_parse_head */
int ulink_http_parse_head(const uint8_t *data, size_t n, ulink_http_head_t *out) {
    memset(out, 0, sizeof(*out));
    out->content_length = -1;
    /* Find the blank line that ends the head. */
    size_t end = 0;
    for (size_t i = 0; i + 3 < n; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            end = i + 4;
            break;
        }
    }
    if (end == 0) {
        return n >= 1024 ? -1 : 0; /* A head longer than 1 KB is not one of ours. */
    }
    char line[160];
    size_t pos = 0;
    bool first = true;
    while (pos < end) {
        size_t e = pos;
        while (e + 1 < end && !(data[e] == '\r' && data[e + 1] == '\n')) {
            e++;
        }
        size_t ll = e - pos;
        if (ll >= sizeof(line)) {
            ll = sizeof(line) - 1;
        }
        memcpy(line, &data[pos], ll);
        line[ll] = '\0';
        pos = e + 2;
        if (ll == 0) {
            break;
        }
        if (first) {
            first = false;
            if (strncmp(line, "HTTP/1.", 7) != 0 || ll < 12) {
                return -1;
            }
            out->status = atoi(&line[9]);
            if (out->status < 100 || out->status > 599) {
                return -1;
            }
        } else if (starts_with_ci(line, "content-type:")) {
            const char *v = line + 13;
            while (*v == ' ') {
                v++;
            }
            strncpy(out->content_type, v, sizeof(out->content_type) - 1);
        } else if (starts_with_ci(line, "content-length:")) {
            out->content_length = atol(line + 15);
        } else if (starts_with_ci(line, "transfer-encoding:") && strstr(line, "chunked") != NULL) {
            out->chunked = true;
        }
    }
    out->head_len = end;
    return 1;
}

/** Chunked decoder states. */
enum { CH_SIZE = 0, CH_SIZE_EXT, CH_SIZE_LF, CH_DATA, CH_DATA_CR, CH_DATA_LF, CH_TRAILER, CH_DONE };

/** @copydoc ulink_chunked_init */
void ulink_chunked_init(ulink_chunked_t *c) {
    c->state = CH_SIZE;
    c->remaining = 0;
    c->size_digits = 0;
}

/** @copydoc ulink_chunked_feed */
long ulink_chunked_feed(ulink_chunked_t *c, const uint8_t *in, size_t n, uint8_t *out, bool *done) {
    size_t o = 0;
    *done = c->state == CH_DONE;
    for (size_t i = 0; i < n && c->state != CH_DONE; i++) {
        const uint8_t b = in[i];
        switch (c->state) {
        case CH_SIZE:
            if (isxdigit(b)) {
                if (++c->size_digits > 6) {
                    return -1;
                }
                c->remaining = (c->remaining << 4) | (size_t)(b <= '9' ? b - '0' : (tolower(b) - 'a' + 10));
            } else if (b == ';') {
                c->state = CH_SIZE_EXT;
            } else if (b == '\r') {
                c->state = CH_SIZE_LF;
            } else {
                return -1;
            }
            break;
        case CH_SIZE_EXT:
            if (b == '\r') {
                c->state = CH_SIZE_LF;
            }
            break;
        case CH_SIZE_LF:
            if (b != '\n') {
                return -1;
            }
            c->size_digits = 0;
            c->state = c->remaining == 0 ? CH_TRAILER : CH_DATA;
            break;
        case CH_DATA:
            out[o++] = b;
            if (--c->remaining == 0) {
                c->state = CH_DATA_CR;
            }
            break;
        case CH_DATA_CR:
            if (b != '\r') {
                return -1;
            }
            c->state = CH_DATA_LF;
            break;
        case CH_DATA_LF:
            if (b != '\n') {
                return -1;
            }
            c->state = CH_SIZE;
            c->remaining = 0;
            break;
        case CH_TRAILER:
            /* After the zero chunk: skip trailer lines until the blank line; we only ever see "\r\n". */
            if (b == '\n') {
                c->state = CH_DONE;
            }
            break;
        default:
            break;
        }
    }
    *done = c->state == CH_DONE;
    return (long)o;
}
