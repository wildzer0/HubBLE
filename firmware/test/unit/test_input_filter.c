/**
 * @file test_input_filter.c
 * @brief Host tests for the anti-chatter / hold-to-register filter.
 */
#include <string.h>

#include "input_filter.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Number of active elements and whether a given one is among them. */
static size_t active(const input_filter_t *f, uint8_t *out) {
    return input_filter_active(f, out, 16);
}
static bool has(const input_filter_t *f, uint8_t id) {
    uint8_t a[16];
    const size_t n = active(f, a);
    for (size_t i = 0; i < n; i++) {
        if (a[i] == id) {
            return true;
        }
    }
    return false;
}

void test_zero_config_is_transparent(void) {
    input_filter_t f;
    input_filter_init(&f, 0, 0);
    const uint8_t a[] = {0x04};
    input_filter_update(&f, 0, a, 1);
    TEST_ASSERT_TRUE(has(&f, 0x04));
    input_filter_update(&f, 1, NULL, 0);
    TEST_ASSERT_FALSE(has(&f, 0x04));
    input_filter_update(&f, 2, a, 1); /* an immediate re-press passes */
    TEST_ASSERT_TRUE(has(&f, 0x04));
}

void test_chatter_is_ignored_until_the_key_is_released(void) {
    input_filter_t f;
    input_filter_init(&f, 50, 0);
    const uint8_t a[] = {0x04};
    input_filter_update(&f, 0, a, 1);
    TEST_ASSERT_TRUE(has(&f, 0x04));
    input_filter_update(&f, 40, NULL, 0); /* released */
    TEST_ASSERT_FALSE(has(&f, 0x04));
    input_filter_update(&f, 60, a, 1); /* 20 ms later: contact bounce */
    TEST_ASSERT_FALSE(has(&f, 0x04));
    input_filter_update(&f, 70, NULL, 0); /* the ghost ends */
    input_filter_update(&f, 200, a, 1);   /* a real press, well after the window */
    TEST_ASSERT_TRUE(has(&f, 0x04));
}

void test_press_after_the_window_is_accepted(void) {
    input_filter_t f;
    input_filter_init(&f, 50, 0);
    const uint8_t a[] = {0x04};
    input_filter_update(&f, 0, a, 1);
    input_filter_update(&f, 100, NULL, 0);
    input_filter_update(&f, 150, a, 1); /* exactly 50 ms after the release */
    TEST_ASSERT_TRUE(has(&f, 0x04));
}

void test_other_keys_are_not_affected_by_one_keys_chatter(void) {
    input_filter_t f;
    input_filter_init(&f, 50, 0);
    const uint8_t a[] = {0x04};
    const uint8_t ab[] = {0x04, 0x05};
    input_filter_update(&f, 0, a, 1);
    input_filter_update(&f, 10, NULL, 0);
    input_filter_update(&f, 20, ab, 2); /* A chatters, B is a fresh key */
    TEST_ASSERT_FALSE(has(&f, 0x04));
    TEST_ASSERT_TRUE(has(&f, 0x05));
}

void test_hold_time_delays_and_a_short_tap_is_dropped(void) {
    input_filter_t f;
    input_filter_init(&f, 0, 100);
    const uint8_t a[] = {0x04};
    input_filter_update(&f, 0, a, 1);
    TEST_ASSERT_FALSE(has(&f, 0x04));
    TEST_ASSERT_TRUE(input_filter_pending(&f));
    input_filter_update(&f, 50, NULL, 0); /* let go after 50 ms: a brush, dropped */
    TEST_ASSERT_FALSE(input_filter_pending(&f));
    input_filter_tick(&f, 500);
    TEST_ASSERT_FALSE(has(&f, 0x04));
}

void test_hold_time_promotes_on_tick_without_new_input(void) {
    input_filter_t f;
    input_filter_init(&f, 0, 100);
    const uint8_t a[] = {0x04};
    input_filter_update(&f, 0, a, 1);
    input_filter_tick(&f, 99);
    TEST_ASSERT_FALSE(has(&f, 0x04));
    input_filter_tick(&f, 100);
    TEST_ASSERT_TRUE(has(&f, 0x04));
    TEST_ASSERT_FALSE(input_filter_pending(&f));
    input_filter_update(&f, 400, NULL, 0);
    TEST_ASSERT_FALSE(has(&f, 0x04));
}

void test_a_dropped_tap_causes_no_lockout(void) {
    input_filter_t f;
    input_filter_init(&f, 200, 100);
    const uint8_t a[] = {0x04};
    input_filter_update(&f, 0, a, 1);
    input_filter_update(&f, 30, NULL, 0); /* dropped tap */
    input_filter_update(&f, 40, a, 1);    /* immediately pressed and held for real */
    input_filter_tick(&f, 140);
    TEST_ASSERT_TRUE(has(&f, 0x04));
}

void test_modifiers_and_wraparound(void) {
    input_filter_t f;
    input_filter_init(&f, 50, 0);
    const uint8_t ctrl[] = {INPUT_FILTER_MOD_BASE};
    input_filter_update(&f, 0xFFFFFFF0u, ctrl, 1);
    TEST_ASSERT_TRUE(has(&f, INPUT_FILTER_MOD_BASE));
    input_filter_update(&f, 0xFFFFFFF8u, NULL, 0);
    input_filter_update(&f, 5, ctrl, 1); /* 13 ms later across the 32-bit wrap: chatter */
    TEST_ASSERT_FALSE(has(&f, INPUT_FILTER_MOD_BASE));
}

void test_many_keys_never_overflow(void) {
    input_filter_t f;
    input_filter_init(&f, 50, 0);
    uint8_t keys[14];
    for (unsigned i = 0; i < sizeof(keys); i++) {
        keys[i] = (uint8_t)(0x04 + i);
    }
    input_filter_update(&f, 0, keys, sizeof(keys));
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT(sizeof(keys), active(&f, out));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_config_is_transparent);
    RUN_TEST(test_chatter_is_ignored_until_the_key_is_released);
    RUN_TEST(test_press_after_the_window_is_accepted);
    RUN_TEST(test_other_keys_are_not_affected_by_one_keys_chatter);
    RUN_TEST(test_hold_time_delays_and_a_short_tap_is_dropped);
    RUN_TEST(test_hold_time_promotes_on_tick_without_new_input);
    RUN_TEST(test_a_dropped_tap_causes_no_lockout);
    RUN_TEST(test_modifiers_and_wraparound);
    RUN_TEST(test_many_keys_never_overflow);
    return UNITY_END();
}
