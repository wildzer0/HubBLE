/**
 * @file test_settings_model.c
 * @brief Host unit tests for settings validation.
 */
#include "settings_model.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Length, character set and default-password rules. */
static void test_password_rules(void) {
    TEST_ASSERT_TRUE(settings_password_valid("correct horse"));
    TEST_ASSERT_TRUE(settings_password_valid("0123456789"));
    TEST_ASSERT_FALSE(settings_password_valid("short"));
    TEST_ASSERT_FALSE(settings_password_valid("123456789"));
    TEST_ASSERT_FALSE(settings_password_valid(NULL));
    TEST_ASSERT_FALSE(settings_password_valid(SETTINGS_DEFAULT_AP_PASSWORD));
    TEST_ASSERT_FALSE(settings_password_valid("tab\there-ok-long"));
    TEST_ASSERT_FALSE(settings_password_valid("caf\xc3\xa9-password-long"));
    char longp[80];
    for (int i = 0; i < 64; i++) {
        longp[i] = 'a';
    }
    longp[64] = '\0';
    TEST_ASSERT_FALSE(settings_password_valid(longp));
    longp[63] = '\0';
    TEST_ASSERT_TRUE(settings_password_valid(longp));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_password_rules);
    return UNITY_END();
}
