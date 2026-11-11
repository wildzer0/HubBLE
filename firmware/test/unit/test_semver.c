/**
 * @file test_semver.c
 * @brief Host unit tests for the semver component.
 */
#include "semver.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Parse helper that fails the test when the string is rejected. */
static semver_t must_parse(const char *s) {
    semver_t v;
    TEST_ASSERT_TRUE_MESSAGE(semver_parse(s, &v), s);
    return v;
}

/** A plain release with leading 'v' parses into its components. */
static void test_parse_release(void) {
    semver_t v = must_parse("v1.22.333");
    TEST_ASSERT_EQUAL_UINT32(1, v.major);
    TEST_ASSERT_EQUAL_UINT32(22, v.minor);
    TEST_ASSERT_EQUAL_UINT32(333, v.patch);
    TEST_ASSERT_EQUAL_STRING("", v.prerelease);
}

/** Pre-release identifiers are preserved. */
static void test_parse_prerelease(void) {
    semver_t v = must_parse("0.2.1-rc.1");
    TEST_ASSERT_EQUAL_STRING("rc.1", v.prerelease);
}

/** Malformed inputs are rejected without touching the output. */
static void test_parse_invalid(void) {
    semver_t v;
    const char *bad[] = {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.x", "1.2.3-", "1.2.3+build", "a.b.c", "1.2.3 "};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(semver_parse(bad[i], &v), bad[i]);
    }
    TEST_ASSERT_FALSE(semver_parse(NULL, &v));
    TEST_ASSERT_FALSE(semver_parse("1.2.3", NULL));
}

/** Numbers compare numerically (10 > 9), not lexically. */
static void test_compare_numeric(void) {
    semver_t a = must_parse("1.10.0"), b = must_parse("1.9.0");
    TEST_ASSERT_GREATER_THAN_INT(0, semver_compare(&a, &b));
    TEST_ASSERT_LESS_THAN_INT(0, semver_compare(&b, &a));
    TEST_ASSERT_EQUAL_INT(0, semver_compare(&a, &a));
}

/** The SemVer 2.0.0 §11 precedence example chain is strictly increasing. */
static void test_compare_spec_chain(void) {
    const char *chain[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (size_t i = 0; i + 1 < sizeof(chain) / sizeof(chain[0]); i++) {
        semver_t lo = must_parse(chain[i]), hi = must_parse(chain[i + 1]);
        TEST_ASSERT_LESS_THAN_INT_MESSAGE(0, semver_compare(&lo, &hi), chain[i]);
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, semver_compare(&hi, &lo), chain[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_release);
    RUN_TEST(test_parse_prerelease);
    RUN_TEST(test_parse_invalid);
    RUN_TEST(test_compare_numeric);
    RUN_TEST(test_compare_spec_chain);
    return UNITY_END();
}
