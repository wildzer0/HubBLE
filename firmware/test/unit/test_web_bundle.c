/**
 * @file test_web_bundle.c
 * @brief Host unit tests for the web bundle reader.
 */
#include <string.h>

#include "unity.h"
#include "web_bundle.h"

void setUp(void) {
}
void tearDown(void) {
}

/* Two entries: "a" -> {1,2,3} and "bc" -> {9}. */
static const uint8_t k_blob[] = {1, 0, 3, 0, 0, 0, 'a', 1, 2, 3, 2, 0, 1, 0, 0, 0, 'b', 'c', 9};

/** Counting a valid blob returns the number of entries. */
static void test_count(void) {
    TEST_ASSERT_EQUAL_INT(2, web_bundle_count(k_blob, sizeof(k_blob)));
    TEST_ASSERT_EQUAL_INT(0, web_bundle_count(k_blob, 0));
}

/** Lookup returns the right payload and reports misses. */
static void test_find(void) {
    web_bundle_entry_t e;
    TEST_ASSERT_EQUAL_INT(1, web_bundle_find(k_blob, sizeof(k_blob), "bc", &e));
    TEST_ASSERT_EQUAL_size_t(1, e.size);
    TEST_ASSERT_EQUAL_UINT8(9, e.data[0]);
    TEST_ASSERT_EQUAL_INT(0, web_bundle_find(k_blob, sizeof(k_blob), "b", &e));
}

/** Truncated blobs are rejected instead of read out of bounds. */
static void test_truncated(void) {
    for (size_t n = 1; n < sizeof(k_blob); n++) {
        if (n == 10) {
            continue; /* exactly one complete entry: valid */
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, web_bundle_count(k_blob, n), "truncated blob accepted");
    }
}

/** Hostile lengths (0xFFFFFFFF) must not overflow bounds checks. */
static void test_hostile_length(void) {
    const uint8_t evil[] = {1, 0, 0xFF, 0xFF, 0xFF, 0xFF, 'x'};
    TEST_ASSERT_EQUAL_INT(-1, web_bundle_count(evil, sizeof(evil)));
}

/** NULL arguments are handled. */
static void test_null(void) {
    web_bundle_entry_t e;
    TEST_ASSERT_EQUAL_INT(-1, web_bundle_count(NULL, 4));
    TEST_ASSERT_EQUAL_INT(-1, web_bundle_find(NULL, 4, "a", &e));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_count);
    RUN_TEST(test_find);
    RUN_TEST(test_truncated);
    RUN_TEST(test_hostile_length);
    RUN_TEST(test_null);
    return UNITY_END();
}
