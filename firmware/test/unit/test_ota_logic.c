/**
 * @file test_ota_logic.c
 * @brief Host unit tests for the board marker scanner and boot guard.
 */
#include <string.h>

#include "board_marker.h"
#include "boot_guard.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Feed a string in fixed-size chunks. */
static void feed_chunked(board_marker_scanner_t *s, const char *text, size_t len, size_t chunk) {
    for (size_t i = 0; i < len; i += chunk) {
        size_t n = len - i < chunk ? len - i : chunk;
        board_marker_feed(s, (const uint8_t *)text + i, n);
    }
}

/** The marker is found regardless of chunk boundaries. */
static void test_marker_any_chunking(void) {
    static const char image[] = "junk\0HUBBLE_BOARD_ID=s3_zero;more junk";
    for (size_t chunk = 1; chunk <= 16; chunk++) {
        board_marker_scanner_t s;
        board_marker_init(&s);
        feed_chunked(&s, image, sizeof(image) - 1, chunk);
        TEST_ASSERT_NOT_NULL_MESSAGE(board_marker_id(&s), "marker not found");
        TEST_ASSERT_EQUAL_STRING("s3_zero", board_marker_id(&s));
    }
}

/** The scanner's own prefix literal (followed by NUL) is skipped; the real marker wins. */
static void test_marker_skips_decoy(void) {
    static const char image[] = "HUBBLE_BOARD_ID=\0xxxHUBBLE_BOARD_ID=xiao_s3;";
    board_marker_scanner_t s;
    board_marker_init(&s);
    feed_chunked(&s, image, sizeof(image) - 1, 5);
    TEST_ASSERT_EQUAL_STRING("xiao_s3", board_marker_id(&s));
}

/** Partial prefixes, missing terminator and empty ids are not markers. */
static void test_marker_invalid(void) {
    static const char *bad[] = {"HUBBLE_BOARD_ID=s3_zero", "HUBBLE_BOARD_ID=;", "HUBBLE_BOARD_I", "nothing here",
                                "HUBBLE_BOARD_ID=UPPER;"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        board_marker_scanner_t s;
        board_marker_init(&s);
        board_marker_feed(&s, (const uint8_t *)bad[i], strlen(bad[i]));
        TEST_ASSERT_NULL_MESSAGE(board_marker_id(&s), bad[i]);
    }
}

/** Overlong ids are rejected rather than overflowing. */
static void test_marker_overlong(void) {
    static const char text[] = "HUBBLE_BOARD_ID=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa;";
    board_marker_scanner_t s;
    board_marker_init(&s);
    board_marker_feed(&s, (const uint8_t *)text, sizeof(text) - 1);
    TEST_ASSERT_NULL(board_marker_id(&s));
}

/** The boot guard only rolls back at the limit and only when possible. */
static void test_boot_guard(void) {
    TEST_ASSERT_EQUAL(BOOT_GUARD_CONTINUE, boot_guard_decide(0, true));
    TEST_ASSERT_EQUAL(BOOT_GUARD_CONTINUE, boot_guard_decide(BOOT_GUARD_MAX_FAILURES - 1, true));
    TEST_ASSERT_EQUAL(BOOT_GUARD_ROLLBACK, boot_guard_decide(BOOT_GUARD_MAX_FAILURES, true));
    TEST_ASSERT_EQUAL(BOOT_GUARD_STUCK, boot_guard_decide(BOOT_GUARD_MAX_FAILURES + 5, false));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_marker_any_chunking);
    RUN_TEST(test_marker_skips_decoy);
    RUN_TEST(test_marker_invalid);
    RUN_TEST(test_marker_overlong);
    RUN_TEST(test_boot_guard);
    return UNITY_END();
}
