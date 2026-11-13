/**
 * @file fuzz_ulink.c
 * @brief Fuzz target: the USB link frame parser (bytes from the host computer, untrusted) and the loopback HTTP
 * helpers.
 */
#include <stdlib.h>
#include <string.h>

#include "ulink_proto.h"

/** Frame sink: touch the payload so the sanitizers see every byte the parser hands out. */
static void sink(uint8_t type, const uint8_t *payload, size_t len, void *ctx) {
    unsigned *sum = ctx;
    *sum += type;
    for (size_t i = 0; i < len; i++) {
        *sum += payload[i];
    }
}

/** libFuzzer entry point. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    unsigned sum = 0;

    /* As a raw stream, in odd-sized pieces, and as reports. */
    ulink_parser_t p;
    ulink_parser_init(&p);
    for (size_t off = 0; off < size;) {
        size_t n = 1 + (size_t)(data[off] % 70);
        if (n > size - off) {
            n = size - off;
        }
        ulink_parser_feed(&p, data + off, n, sink, &sum);
        off += n;
    }
    ulink_parser_init(&p);
    for (size_t off = 0; off + ULINK_REPORT_SIZE <= size; off += ULINK_REPORT_SIZE) {
        ulink_parser_feed_report(&p, data + off, ULINK_REPORT_SIZE, sink, &sum);
    }

    /* The HTTP helpers see whatever the loopback server answers. */
    ulink_http_head_t head;
    ulink_http_parse_head(data, size, &head);
    ulink_chunked_t c;
    ulink_chunked_init(&c);
    uint8_t *out = malloc(size + 1);
    if (out != NULL) {
        bool done;
        ulink_chunked_feed(&c, data, size, out, &done);
        free(out);
    }
    return (int)(sum & 0); /* Always 0; the sum only keeps the loops from being optimised away. */
}
