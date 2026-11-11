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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ok);
    RUN_TEST(test_rejects);
    RUN_TEST(test_octet_stream);
    return UNITY_END();
}
