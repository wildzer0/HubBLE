/**
 * @file test_ulink_proto.c
 * @brief Host tests for the USB configuration link framing and HTTP helpers.
 *
 * The expected byte sequences are mirrored in webapp/tests/unit/usbLink.test.ts so both ends agree.
 */
#include <string.h>

#include "ulink_proto.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

typedef struct {
    int frames;
    uint8_t last_type;
    size_t last_len;
    uint8_t last[ULINK_MAX_PAYLOAD];
} sink_t;

static void on_frame(uint8_t type, const uint8_t *payload, size_t len, void *ctx) {
    sink_t *s = ctx;
    s->frames++;
    s->last_type = type;
    s->last_len = len;
    memcpy(s->last, payload, len);
}

void test_header_and_report_encoding(void) {
    uint8_t h[3];
    ulink_encode_header(ULINK_HELLO, 0x0105, h);
    TEST_ASSERT_EQUAL_HEX8(0x05, h[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, h[1]);
    TEST_ASSERT_EQUAL_HEX8(0x51, h[2]);
    uint8_t r[ULINK_REPORT_SIZE];
    const uint8_t d[] = {1, 2, 3};
    ulink_pack_report(r, d, sizeof(d));
    TEST_ASSERT_EQUAL_UINT8(3, r[0]);
    TEST_ASSERT_EQUAL_UINT8(3, r[3]);
    TEST_ASSERT_EQUAL_UINT8(0, r[4]); /* padding */
}

void test_frame_split_across_reports(void) {
    /* A 100-byte frame needs 2 reports (63 + 40 bytes). */
    uint8_t stream[3 + 100];
    ulink_encode_header(ULINK_REQ_BODY, 100, stream);
    for (int i = 0; i < 100; i++) {
        stream[3 + i] = (uint8_t)i;
    }
    ulink_parser_t p;
    sink_t s = {0};
    ulink_parser_init(&p);
    uint8_t r[ULINK_REPORT_SIZE];
    ulink_pack_report(r, stream, ULINK_REPORT_DATA);
    ulink_parser_feed_report(&p, r, sizeof(r), on_frame, &s);
    TEST_ASSERT_EQUAL_INT(0, s.frames);
    ulink_pack_report(r, stream + ULINK_REPORT_DATA, sizeof(stream) - ULINK_REPORT_DATA);
    ulink_parser_feed_report(&p, r, sizeof(r), on_frame, &s);
    TEST_ASSERT_EQUAL_INT(1, s.frames);
    TEST_ASSERT_EQUAL_UINT8(ULINK_REQ_BODY, s.last_type);
    TEST_ASSERT_EQUAL_UINT(100, s.last_len);
    TEST_ASSERT_EQUAL_UINT8(99, s.last[99]);
}

void test_two_frames_in_one_report_and_empty_frame(void) {
    uint8_t stream[6];
    ulink_encode_header(ULINK_ACK, 0, stream);
    ulink_encode_header(ULINK_RES_END, 0, stream + 3);
    ulink_parser_t p;
    sink_t s = {0};
    ulink_parser_init(&p);
    ulink_parser_feed(&p, stream, sizeof(stream), on_frame, &s);
    TEST_ASSERT_EQUAL_INT(2, s.frames);
    TEST_ASSERT_EQUAL_UINT8(ULINK_RES_END, s.last_type);
}

void test_oversized_frame_resynchronises(void) {
    ulink_parser_t p;
    sink_t s = {0};
    ulink_parser_init(&p);
    const uint8_t bad[] = {0xFF, 0xFF, 0x01}; /* announces 65535 bytes */
    ulink_parser_feed(&p, bad, sizeof(bad), on_frame, &s);
    TEST_ASSERT_EQUAL_UINT32(1, p.errors);
    uint8_t good[3];
    ulink_encode_header(ULINK_ACK, 0, good);
    ulink_parser_feed(&p, good, sizeof(good), on_frame, &s);
    TEST_ASSERT_EQUAL_INT(1, s.frames);
}

void test_report_count_is_clamped(void) {
    /* A frame announcing 100 bytes, in a report whose count byte lies (0xFF): only the 63 real bytes are consumed. */
    uint8_t r[ULINK_REPORT_SIZE] = {0xFF, 100, 0, ULINK_RES_BODY};
    ulink_parser_t p;
    sink_t s = {0};
    ulink_parser_init(&p);
    ulink_parser_feed_report(&p, r, sizeof(r), on_frame, &s);
    TEST_ASSERT_EQUAL_INT(0, s.frames);
    TEST_ASSERT_EQUAL_UINT(ULINK_REPORT_DATA, p.have);
}

void test_http_head_parsing(void) {
    const char *h =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 42\r\nConnection: close\r\n\r\n{}";
    ulink_http_head_t out;
    TEST_ASSERT_EQUAL_INT(1, ulink_http_parse_head((const uint8_t *)h, strlen(h), &out));
    TEST_ASSERT_EQUAL_INT(200, out.status);
    TEST_ASSERT_EQUAL_STRING("application/json", out.content_type);
    TEST_ASSERT_EQUAL_INT(42, out.content_length);
    TEST_ASSERT_FALSE(out.chunked);
    TEST_ASSERT_EQUAL_UINT(strlen(h) - 2, out.head_len);
}

void test_http_head_incomplete_chunked_and_garbage(void) {
    const char *part = "HTTP/1.1 409 Conflict\r\nContent-Ty";
    ulink_http_head_t out;
    TEST_ASSERT_EQUAL_INT(0, ulink_http_parse_head((const uint8_t *)part, strlen(part), &out));
    const char *ch = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(1, ulink_http_parse_head((const uint8_t *)ch, strlen(ch), &out));
    TEST_ASSERT_TRUE(out.chunked);
    TEST_ASSERT_EQUAL_INT(-1, out.content_length);
    const char *junk = "GET / HTTP/1.1\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(-1, ulink_http_parse_head((const uint8_t *)junk, strlen(junk), &out));
}

void test_chunked_decoding_across_arbitrary_splits(void) {
    const char *enc = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
    const size_t n = strlen(enc);
    for (size_t split = 1; split < n; split++) {
        ulink_chunked_t c;
        ulink_chunked_init(&c);
        uint8_t out[64];
        size_t total = 0;
        bool done = false;
        long a = ulink_chunked_feed(&c, (const uint8_t *)enc, split, out, &done);
        TEST_ASSERT_TRUE(a >= 0);
        total += (size_t)a;
        long b = ulink_chunked_feed(&c, (const uint8_t *)enc + split, n - split, out + total, &done);
        TEST_ASSERT_TRUE(b >= 0);
        total += (size_t)b;
        TEST_ASSERT_TRUE(done);
        TEST_ASSERT_EQUAL_UINT(11, total);
        TEST_ASSERT_EQUAL_MEMORY("Hello World", out, 11);
    }
}

void test_chunked_rejects_garbage(void) {
    ulink_chunked_t c;
    ulink_chunked_init(&c);
    uint8_t out[16];
    bool done;
    TEST_ASSERT_EQUAL_INT(-1, ulink_chunked_feed(&c, (const uint8_t *)"zz\r\n", 4, out, &done));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_header_and_report_encoding);
    RUN_TEST(test_frame_split_across_reports);
    RUN_TEST(test_two_frames_in_one_report_and_empty_frame);
    RUN_TEST(test_oversized_frame_resynchronises);
    RUN_TEST(test_report_count_is_clamped);
    RUN_TEST(test_http_head_parsing);
    RUN_TEST(test_http_head_incomplete_chunked_and_garbage);
    RUN_TEST(test_chunked_decoding_across_arbitrary_splits);
    RUN_TEST(test_chunked_rejects_garbage);
    return UNITY_END();
}
