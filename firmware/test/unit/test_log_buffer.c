/**
 * @file test_log_buffer.c
 * @brief Host unit tests for the log ring buffer.
 */
#include <string.h>

#include "log_buffer.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Data written is read back in order. */
static void test_write_read(void) {
    char store[16];
    log_buffer_t lb;
    log_buffer_init(&lb, store, sizeof(store));
    log_buffer_write(&lb, "abc\n", 4);
    char out[16];
    uint64_t next;
    size_t n = log_buffer_read(&lb, 0, out, sizeof(out), false, &next);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_MEMORY("abc\n", out, 4);
    TEST_ASSERT_EQUAL_UINT64(4, next);
}

/** Wrapping overwrites the oldest bytes and a stale cursor is clamped. */
static void test_wrap_and_stale_cursor(void) {
    char store[8];
    log_buffer_t lb;
    log_buffer_init(&lb, store, sizeof(store));
    log_buffer_write(&lb, "0123456789", 10); /* only "23456789" survives */
    char out[16];
    uint64_t next;
    size_t n = log_buffer_read(&lb, 0, out, sizeof(out), false, &next);
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_MEMORY("23456789", out, 8);
    log_buffer_write(&lb, "AB", 2);
    n = log_buffer_read(&lb, next, out, sizeof(out), false, &next);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_MEMORY("AB", out, 2);
}

/** whole_lines never returns a partial line; it is delivered once complete. */
static void test_whole_lines(void) {
    char store[64];
    log_buffer_t lb;
    log_buffer_init(&lb, store, sizeof(store));
    log_buffer_write(&lb, "one\ntw", 6);
    char out[64];
    uint64_t next;
    size_t n = log_buffer_read(&lb, 0, out, sizeof(out), true, &next);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_MEMORY("one\n", out, 4);
    log_buffer_write(&lb, "o\n", 2);
    n = log_buffer_read(&lb, next, out, sizeof(out), true, &next);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_MEMORY("two\n", out, 4);
}

/** A cursor from the future (e.g. after a reboot) is clamped, not misread. */
static void test_future_cursor(void) {
    char store[8];
    log_buffer_t lb;
    log_buffer_init(&lb, store, sizeof(store));
    log_buffer_write(&lb, "x\n", 2);
    char out[8];
    uint64_t next;
    TEST_ASSERT_EQUAL_size_t(0, log_buffer_read(&lb, 999, out, sizeof(out), true, &next));
    TEST_ASSERT_EQUAL_UINT64(2, next);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_read);
    RUN_TEST(test_wrap_and_stale_cursor);
    RUN_TEST(test_whole_lines);
    RUN_TEST(test_future_cursor);
    return UNITY_END();
}
