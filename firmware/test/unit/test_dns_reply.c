/**
 * @file test_dns_reply.c
 * @brief Host unit tests for the captive-portal DNS reply builder.
 */
#include <string.h>

#include "dns_reply.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

static const uint8_t k_ip[4] = {192, 168, 4, 1};

/* Query: id 0x1234, RD set, 1 question "a.bc" type A class IN. */
static const uint8_t k_query_a[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 1,    'a',  2,    'b',  'c',  0,    0x00, 0x01, 0x00, 0x01};

/** An A query gets one answer with our IP. */
static void test_a_query(void) {
    uint8_t out[128];
    size_t n = dns_build_reply(k_query_a, sizeof(k_query_a), k_ip, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(k_query_a) + 16, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, out[0]); /* id echoed */
    TEST_ASSERT_BITS_HIGH(0x80, out[2]);  /* QR */
    TEST_ASSERT_EQUAL_UINT8(1, out[7]);   /* ANCOUNT */
    TEST_ASSERT_EQUAL_MEMORY(k_ip, out + n - 4, 4);
}

/** An AAAA query is answered with an empty NOERROR reply. */
static void test_aaaa_query(void) {
    uint8_t q[sizeof(k_query_a)];
    memcpy(q, k_query_a, sizeof(q));
    q[sizeof(q) - 3] = 28; /* QTYPE = AAAA */
    uint8_t out[128];
    size_t n = dns_build_reply(q, sizeof(q), k_ip, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(q), n);
    TEST_ASSERT_EQUAL_UINT8(0, out[7]);
}

/** Malformed, truncated, response and oversized packets are dropped. */
static void test_rejects_bad_packets(void) {
    uint8_t out[128];
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(k_query_a, 5, k_ip, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(k_query_a, sizeof(k_query_a) - 1, k_ip, out, sizeof(out)));
    uint8_t resp[sizeof(k_query_a)];
    memcpy(resp, k_query_a, sizeof(resp));
    resp[2] |= 0x80; /* QR=1: never answer a response */
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(resp, sizeof(resp), k_ip, out, sizeof(out)));
    uint8_t ptr[sizeof(k_query_a)];
    memcpy(ptr, k_query_a, sizeof(ptr));
    ptr[12] = 0xC0; /* compression pointer in a question */
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(ptr, sizeof(ptr), k_ip, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(k_query_a, sizeof(k_query_a), k_ip, out, 10));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_query);
    RUN_TEST(test_aaaa_query);
    RUN_TEST(test_rejects_bad_packets);
    return UNITY_END();
}
