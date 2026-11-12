/**
 * @file test_http_guard.c
 * @brief Host unit tests for the CSRF guard.
 */
#include "http_guard.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

#define JSON "application/json"

/** A well-formed same-origin or non-browser request passes. */
static void test_ok(void) {
    TEST_ASSERT_EQUAL(GUARD_OK, http_guard_check("1", "application/json; charset=utf-8", JSON, NULL, "hubble.local"));
    TEST_ASSERT_EQUAL(GUARD_OK, http_guard_check("1", JSON, JSON, "http://hubble.local", "hubble.local"));
    TEST_ASSERT_EQUAL(GUARD_OK, http_guard_check("1", JSON, JSON, "http://192.168.4.1", "192.168.4.1"));
}

/** Missing or wrong header, content type or foreign origin are rejected. */
static void test_rejects(void) {
    TEST_ASSERT_EQUAL(GUARD_ERR_HEADER, http_guard_check(NULL, JSON, JSON, NULL, "h"));
    TEST_ASSERT_EQUAL(GUARD_ERR_HEADER, http_guard_check("0", JSON, JSON, NULL, "h"));
    TEST_ASSERT_EQUAL(GUARD_ERR_CONTENT_TYPE, http_guard_check("1", "text/plain", JSON, NULL, "h"));
    TEST_ASSERT_EQUAL(GUARD_ERR_CONTENT_TYPE, http_guard_check("1", NULL, JSON, NULL, "h"));
    TEST_ASSERT_EQUAL(GUARD_ERR_ORIGIN, http_guard_check("1", JSON, JSON, "http://evil.example", "hubble.local"));
    TEST_ASSERT_EQUAL(GUARD_ERR_ORIGIN, http_guard_check("1", JSON, JSON, "http://hubble.local", NULL));
}

/** Binary uploads use their own expected content type. */
static void test_octet_stream(void) {
    TEST_ASSERT_EQUAL(GUARD_OK,
                      http_guard_check("1", "application/octet-stream", "application/octet-stream", NULL, "h"));
    TEST_ASSERT_EQUAL(GUARD_ERR_CONTENT_TYPE, http_guard_check("1", JSON, "application/octet-stream", NULL, "h"));
}

/** Only the SoftAP address and hubble.local (optionally with a port) are "our" host names. */
static void test_host_ours(void) {
    TEST_ASSERT_TRUE(http_guard_host_ours("192.168.4.1"));
    TEST_ASSERT_TRUE(http_guard_host_ours("192.168.4.1:80"));
    TEST_ASSERT_TRUE(http_guard_host_ours("HubBLE.local"));
    TEST_ASSERT_FALSE(http_guard_host_ours("192.168.4.10"));
    TEST_ASSERT_FALSE(http_guard_host_ours("192.168.4.1.evil.example"));
    TEST_ASSERT_FALSE(http_guard_host_ours("hubble.local.evil.example"));
    TEST_ASSERT_FALSE(http_guard_host_ours("192.168.4.1:"));
    TEST_ASSERT_FALSE(http_guard_host_ours("192.168.4.1:8a"));
    TEST_ASSERT_FALSE(http_guard_host_ours("evil.example"));
    TEST_ASSERT_FALSE(http_guard_host_ours(NULL));
}

/** The WebSocket handshake needs our host and a same-host (or absent) origin. */
static void test_ws_ok(void) {
    TEST_ASSERT_TRUE(http_guard_ws_ok("http://hubble.local", "hubble.local"));
    TEST_ASSERT_TRUE(http_guard_ws_ok(NULL, "192.168.4.1"));
    TEST_ASSERT_FALSE(http_guard_ws_ok("http://evil.example", "hubble.local"));
    TEST_ASSERT_FALSE(http_guard_ws_ok("http://evil.example", "evil.example"));
    TEST_ASSERT_FALSE(http_guard_ws_ok(NULL, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ok);
    RUN_TEST(test_rejects);
    RUN_TEST(test_octet_stream);
    RUN_TEST(test_host_ours);
    RUN_TEST(test_ws_ok);
    return UNITY_END();
}
