/**
 * @file test_combo_lat.c
 * @brief Host tests for combo (Ctrl + Alt / recovery) and lat_stats.
 */
#include "combo.h"
#include "lat_stats.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

#define BOTH (COMBO_MOD_LCTRL | COMBO_MOD_LALT)

void test_ap_on_after_hold(void) {
    combo_t c;
    combo_init(&c, 0);
    const uint8_t none[6] = {0};
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 1000, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 3999, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_AP_ON, combo_update(&c, 4000, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 9000, BOTH, none, 6)); /* once per hold */
    /* Release re-arms. */
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 9100, 0, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 9200, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_AP_ON, combo_update(&c, 12200, BOTH, none, 6));
}

void test_one_modifier_or_other_key_does_not_fire(void) {
    combo_t c;
    combo_init(&c, 3000);
    const uint8_t none[6] = {0};
    const uint8_t a[6] = {0x04};
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 0, COMBO_MOD_LCTRL, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 5000, COMBO_MOD_LALT, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 6000, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 8000, BOTH, a, 6));                        /* a key interrupts */
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 20000, BOTH | COMBO_MOD_LSHIFT, none, 6)); /* extra modifier */
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 30000, BOTH | 0x08, none, 6));             /* GUI */
}

void test_either_side_counts(void) {
    combo_t c;
    combo_init(&c, 3000);
    const uint8_t none[6] = {0};
    const uint8_t mods = COMBO_MOD_RCTRL | COMBO_MOD_RALT;
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 0, mods, none, 6));
    TEST_ASSERT_EQUAL(COMBO_AP_ON, combo_update(&c, 3000, mods, none, 6));
}

void test_recovery_after_10s_with_esc(void) {
    combo_t c;
    combo_init(&c, 3000);
    const uint8_t none[6] = {0};
    const uint8_t esc[6] = {COMBO_KEY_ESC};
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 0, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 500, BOTH, esc, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 9999, BOTH, esc, 6));
    TEST_ASSERT_EQUAL(COMBO_RECOVERY, combo_update(&c, 10000, BOTH, esc, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 11000, BOTH, esc, 6));
}

void test_esc_phase_never_fires_ap_on(void) {
    combo_t c;
    combo_init(&c, 3000);
    const uint8_t esc[6] = {COMBO_KEY_ESC};
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 0, BOTH, esc, 6));
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, 4000, BOTH, esc, 6));
}

void test_clock_wraparound(void) {
    combo_t c;
    combo_init(&c, 3000);
    const uint8_t none[6] = {0};
    uint32_t t0 = 0xFFFFFF00u;
    TEST_ASSERT_EQUAL(COMBO_NONE, combo_update(&c, t0, BOTH, none, 6));
    TEST_ASSERT_EQUAL(COMBO_AP_ON, combo_update(&c, t0 + 3000, BOTH, none, 6));
}

void test_lat_stats(void) {
    lat_stats_t s;
    lat_summary_t r;
    lat_stats_reset(&s);
    lat_stats_summary(&s, &r);
    TEST_ASSERT_EQUAL_UINT32(0, r.count);
    for (int i = 0; i < 100; i++) {
        lat_stats_record(&s, 1000 + (uint32_t)i * 10); /* 1000..1990 us */
    }
    lat_stats_summary(&s, &r);
    TEST_ASSERT_EQUAL_UINT32(100, r.count);
    TEST_ASSERT_EQUAL_UINT32(1000, r.min_us);
    TEST_ASSERT_EQUAL_UINT32(1990, r.max_us);
    TEST_ASSERT_EQUAL_UINT32(1495, r.avg_us);
    /* 95th sample is 1940 us, in the bucket [1750, 2000): reported as its edge capped at the max. */
    TEST_ASSERT_EQUAL_UINT32(1990, r.p95_us);
}

void test_lat_stats_overflow_bucket(void) {
    lat_stats_t s;
    lat_summary_t r;
    lat_stats_reset(&s);
    for (int i = 0; i < 19; i++) {
        lat_stats_record(&s, 500);
    }
    lat_stats_record(&s, 50000); /* 5 % of the samples are huge: p95 must still be small */
    lat_stats_summary(&s, &r);
    TEST_ASSERT_EQUAL_UINT32(750, r.p95_us); /* upper edge of the 500 us bucket */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ap_on_after_hold);
    RUN_TEST(test_one_modifier_or_other_key_does_not_fire);
    RUN_TEST(test_either_side_counts);
    RUN_TEST(test_recovery_after_10s_with_esc);
    RUN_TEST(test_esc_phase_never_fires_ap_on);
    RUN_TEST(test_clock_wraparound);
    RUN_TEST(test_lat_stats);
    RUN_TEST(test_lat_stats_overflow_bucket);
    return UNITY_END();
}
