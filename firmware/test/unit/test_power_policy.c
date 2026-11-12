/**
 * @file test_power_policy.c
 * @brief Host unit tests for the power policy.
 */
#include "power_policy.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Low power starts only after the debounce time; a short suspend never gets there. */
static void test_suspend_is_debounced(void) {
    power_tracker_t t;
    power_tracker_init(&t, 2000);
    TEST_ASSERT_EQUAL(POWER_ACTIVE, power_tracker_step(&t, 1000, false));
    TEST_ASSERT_EQUAL(POWER_ACTIVE, power_tracker_step(&t, 1100, true));
    TEST_ASSERT_EQUAL(POWER_ACTIVE, power_tracker_step(&t, 2000, true));
    TEST_ASSERT_EQUAL(POWER_LOW, power_tracker_step(&t, 3100, true));
    TEST_ASSERT_EQUAL(POWER_LOW, power_tracker_step(&t, 9000, true));
}

/** A flap (suspend, resume, suspend) restarts the debounce from the last suspend. */
static void test_flap_restarts_debounce(void) {
    power_tracker_t t;
    power_tracker_init(&t, 2000);
    power_tracker_step(&t, 0, true);
    power_tracker_step(&t, 1500, false);
    TEST_ASSERT_EQUAL(POWER_ACTIVE, power_tracker_step(&t, 1600, true));
    TEST_ASSERT_EQUAL(POWER_ACTIVE, power_tracker_step(&t, 3000, true)); /* only 1.4 s since the second suspend */
    TEST_ASSERT_EQUAL(POWER_LOW, power_tracker_step(&t, 3600, true));
}

/** Resume is immediate, even from low power. */
static void test_resume_is_immediate(void) {
    power_tracker_t t;
    power_tracker_init(&t, 500);
    power_tracker_step(&t, 0, true);
    TEST_ASSERT_EQUAL(POWER_LOW, power_tracker_step(&t, 600, true));
    TEST_ASSERT_EQUAL(POWER_ACTIVE, power_tracker_step(&t, 601, false));
}

/** Zero delay means low power on the first suspended sample. */
static void test_zero_delay(void) {
    power_tracker_t t;
    power_tracker_init(&t, 0);
    TEST_ASSERT_EQUAL(POWER_LOW, power_tracker_step(&t, 10, true));
}

/** The generated low-power profile is always valid and keeps its interval and latency. */
static void test_low_profile_valid(void) {
    for (uint16_t itvl = 24; itvl <= 80; itvl += 8) {
        for (uint16_t lat = 0; lat <= 30; lat += 5) {
            power_profile_t p = power_low_profile(itvl, lat);
            TEST_ASSERT_EQUAL_UINT16(itvl, p.itvl_units);
            TEST_ASSERT_EQUAL_UINT16(lat, p.latency);
            TEST_ASSERT_TRUE_MESSAGE(power_profile_valid(&p), "generated profile must be valid");
            TEST_ASSERT_GREATER_OR_EQUAL_UINT16(400, p.timeout_10ms);
        }
    }
    /* Default choice: 40 ms interval, latency 10 -> 4 s timeout. */
    power_profile_t d = power_low_profile(32, 10);
    TEST_ASSERT_EQUAL_UINT16(400, d.timeout_10ms);
}

/** Validation catches the classic mistakes. */
static void test_profile_validation(void) {
    power_profile_t fast = {.itvl_units = 6, .latency = 0, .timeout_10ms = 200};
    TEST_ASSERT_TRUE(power_profile_valid(&fast));
    power_profile_t short_timeout = {.itvl_units = 32, .latency = 10, .timeout_10ms = 50}; /* 0.5 s < 0.88 s */
    TEST_ASSERT_FALSE(power_profile_valid(&short_timeout));
    power_profile_t bad_itvl = {.itvl_units = 3, .latency = 0, .timeout_10ms = 200};
    TEST_ASSERT_FALSE(power_profile_valid(&bad_itvl));
    power_profile_t bad_lat = {.itvl_units = 32, .latency = 500, .timeout_10ms = 3200};
    TEST_ASSERT_FALSE(power_profile_valid(&bad_lat));
    /* A huge interval and latency exceed even the maximum timeout: the builder clamps, validation then rejects. */
    power_profile_t big = power_low_profile(3200, 499);
    TEST_ASSERT_FALSE(power_profile_valid(&big));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_suspend_is_debounced);
    RUN_TEST(test_flap_restarts_debounce);
    RUN_TEST(test_resume_is_immediate);
    RUN_TEST(test_zero_delay);
    RUN_TEST(test_low_profile_valid);
    RUN_TEST(test_profile_validation);
    return UNITY_END();
}
