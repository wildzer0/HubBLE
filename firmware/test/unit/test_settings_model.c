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

/** Station credentials: SSID 1..32 chars, password empty or 8..63 printable. */
static void test_sta_valid(void) {
    TEST_ASSERT_TRUE(settings_sta_valid("Home", "password1"));
    TEST_ASSERT_TRUE(settings_sta_valid("Open net", ""));
    TEST_ASSERT_FALSE(settings_sta_valid("", "password1"));
    TEST_ASSERT_FALSE(settings_sta_valid("0123456789012345678901234567890123", "password1"));
    TEST_ASSERT_FALSE(settings_sta_valid("Ho\nme", "password1"));
    TEST_ASSERT_FALSE(settings_sta_valid("Home", "short"));
    TEST_ASSERT_FALSE(settings_sta_valid("Home", "pass\x01word1"));
    TEST_ASSERT_FALSE(settings_sta_valid(NULL, ""));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sta_valid);
    RUN_TEST(test_password_rules);
    return UNITY_END();
}
