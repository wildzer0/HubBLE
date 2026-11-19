/** Host tests of the ASCII -> HID mapping (US layout). */
#include <string.h>

#include "ascii_hid.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

static ascii_hid_key_t key(char c) {
    ascii_hid_key_t k = {0xFF, 0xFF};
    TEST_ASSERT_TRUE(ascii_hid_lookup(c, &k));
    return k;
}

void test_letters_and_digits(void) {
    TEST_ASSERT_EQUAL_HEX8(0x04, key('a').usage);
    TEST_ASSERT_EQUAL_HEX8(0, key('a').modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x1D, key('z').usage);
    TEST_ASSERT_EQUAL_HEX8(0x04, key('A').usage);
    TEST_ASSERT_EQUAL_HEX8(ASCII_HID_SHIFT, key('A').modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x1E, key('1').usage);
    TEST_ASSERT_EQUAL_HEX8(0x26, key('9').usage);
    TEST_ASSERT_EQUAL_HEX8(0x27, key('0').usage);
}

void test_shifted_digits_and_punctuation(void) {
    TEST_ASSERT_EQUAL_HEX8(0x1E, key('!').usage);
    TEST_ASSERT_EQUAL_HEX8(ASCII_HID_SHIFT, key('!').modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x1F, key('@').usage);
    TEST_ASSERT_EQUAL_HEX8(0x27, key(')').usage);
    TEST_ASSERT_EQUAL_HEX8(ASCII_HID_SHIFT, key(')').modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x2D, key('-').usage);
    TEST_ASSERT_EQUAL_HEX8(0, key('-').modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x2D, key('_').usage);
    TEST_ASSERT_EQUAL_HEX8(ASCII_HID_SHIFT, key('_').modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x38, key('/').usage);
    TEST_ASSERT_EQUAL_HEX8(0x38, key('?').usage);
    TEST_ASSERT_EQUAL_HEX8(0x31, key('\\').usage);
    TEST_ASSERT_EQUAL_HEX8(0x34, key('"').usage);
    TEST_ASSERT_EQUAL_HEX8(0x35, key('~').usage);
}

void test_space_enter_tab(void) {
    TEST_ASSERT_EQUAL_HEX8(0x2C, key(' ').usage);
    TEST_ASSERT_EQUAL_HEX8(0x28, key('\n').usage);
    TEST_ASSERT_EQUAL_HEX8(0x2B, key('\t').usage);
}

/** Every printable ASCII character has a key, and no two characters share the same key + modifier pair. */
void test_all_printable_unique(void) {
    for (int a = 0x20; a < 0x7F; a++) {
        for (int b = a + 1; b < 0x7F; b++) {
            ascii_hid_key_t ka = key((char)a);
            ascii_hid_key_t kb = key((char)b);
            TEST_ASSERT_TRUE(ka.usage != kb.usage || ka.modifiers != kb.modifiers);
        }
    }
}

void test_unmapped_is_rejected_and_untouched(void) {
    ascii_hid_key_t k = {0xAA, 0xBB};
    TEST_ASSERT_FALSE(ascii_hid_lookup('\x01', &k));
    TEST_ASSERT_FALSE(ascii_hid_lookup((char)0xC3, &k)); /* first byte of a UTF-8 sequence */
    TEST_ASSERT_FALSE(ascii_hid_lookup('\x7F', &k));
    TEST_ASSERT_EQUAL_HEX8(0xAA, k.usage);
    TEST_ASSERT_EQUAL_HEX8(0xBB, k.modifiers);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_letters_and_digits);
    RUN_TEST(test_shifted_digits_and_punctuation);
    RUN_TEST(test_space_enter_tab);
    RUN_TEST(test_all_printable_unique);
    RUN_TEST(test_unmapped_is_rejected_and_untouched);
    return UNITY_END();
}
