/**
 * @file test_hid_report.c
 * @brief Host unit tests for hid_report: descriptor sanity and the multi-source merge logic.
 */
#include <string.h>

#include "hid_report.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Walk a descriptor and check that items are well formed, collections are balanced and sizes add up. */
static void check_descriptor(const uint8_t *d, size_t len, unsigned expect_collections) {
    size_t i = 0;
    int depth = 0;
    unsigned collections = 0;
    while (i < len) {
        uint8_t prefix = d[i];
        size_t size = prefix & 0x03;
        if (size == 3) {
            size = 4;
        }
        TEST_ASSERT_TRUE_MESSAGE(i + 1 + size <= len, "item overruns the descriptor");
        if ((prefix & 0xFC) == 0xA0) { /* Collection */
            depth++;
            collections++;
        } else if ((prefix & 0xFC) == 0xC0) { /* End Collection */
            depth--;
            TEST_ASSERT_TRUE(depth >= 0);
        }
        i += 1 + size;
    }
    TEST_ASSERT_EQUAL_INT(0, depth);
    TEST_ASSERT_EQUAL_UINT(expect_collections, collections);
}

void test_descriptors_well_formed(void) {
    check_descriptor(hid_kbd_report_desc, HID_KBD_REPORT_DESC_LEN, 1);
    check_descriptor(hid_ctrl_report_desc, HID_CTRL_REPORT_DESC_LEN, 4); /* mouse + pointer + consumer + system */
}

void test_report_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(HID_KBD_REPORT_SIZE, sizeof(hid_kbd_report_t));
    TEST_ASSERT_EQUAL_UINT(HID_MOUSE_REPORT_SIZE, sizeof(hid_mouse_in_t));
}

void test_keyboard_union_and_dedupe(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    const uint8_t a[] = {0x04, 0x05};
    const uint8_t b[] = {0x05, 0x06};
    hid_merge_set_keyboard(&m, 0, 0x02, a, 2);
    hid_merge_set_keyboard(&m, 1, 0x01, b, 2);
    hid_kbd_report_t r;
    hid_merge_get_keyboard(&m, &r);
    TEST_ASSERT_EQUAL_HEX8(0x03, r.modifiers);
    const uint8_t expect[HID_KBD_MAX_KEYS] = {0x04, 0x05, 0x06, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, r.keys, HID_KBD_MAX_KEYS);
}

void test_keyboard_rollover_error(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    const uint8_t a[] = {4, 5, 6, 7, 8, 9};
    const uint8_t b[] = {10};
    hid_merge_set_keyboard(&m, 0, 0, a, 6);
    hid_merge_set_keyboard(&m, 1, 0, b, 1);
    hid_kbd_report_t r;
    hid_merge_get_keyboard(&m, &r);
    for (int i = 0; i < HID_KBD_MAX_KEYS; i++) {
        TEST_ASSERT_EQUAL_HEX8(HID_KBD_ROLLOVER_ERROR, r.keys[i]);
    }
}

void test_keyboard_ignores_modifier_usages_and_zero(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    const uint8_t a[] = {0xE1, 0, 0x04};
    hid_merge_set_keyboard(&m, 0, 0, a, 3);
    hid_kbd_report_t r;
    hid_merge_get_keyboard(&m, &r);
    TEST_ASSERT_EQUAL_HEX8(0x04, r.keys[0]);
    TEST_ASSERT_EQUAL_HEX8(0, r.keys[1]);
}

void test_clear_source_releases_keys(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    const uint8_t a[] = {4};
    hid_merge_set_keyboard(&m, 2, 0x02, a, 1);
    hid_merge_set_consumer(&m, 2, 0xE9);
    hid_merge_mouse(&m, 2, HID_MOUSE_LEFT, 5, 0, 0, 0);
    hid_merge_clear_source(&m, 2);
    hid_kbd_report_t k;
    hid_merge_get_keyboard(&m, &k);
    TEST_ASSERT_EQUAL_HEX8(0, k.modifiers);
    TEST_ASSERT_EQUAL_HEX8(0, k.keys[0]);
    TEST_ASSERT_EQUAL_UINT16(0, hid_merge_get_consumer(&m));
    TEST_ASSERT_FALSE(hid_merge_mouse_pending(&m));
}

void test_out_of_range_source_is_ignored(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_set_keyboard(&m, HID_MERGE_SOURCES, 0xFF, NULL, 0);
    hid_merge_mouse(&m, 99, 1, 1, 1, 1, 1);
    hid_merge_clear_source(&m, 99);
    hid_kbd_report_t k;
    hid_merge_get_keyboard(&m, &k);
    TEST_ASSERT_EQUAL_HEX8(0, k.modifiers);
    TEST_ASSERT_FALSE(hid_merge_mouse_pending(&m));
}

void test_consumer_first_source_wins_and_range(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_set_consumer(&m, 1, 0xE9);
    hid_merge_set_consumer(&m, 0, 0xEA);
    TEST_ASSERT_EQUAL_UINT16(0xEA, hid_merge_get_consumer(&m));
    hid_merge_set_consumer(&m, 0, 0x1000); /* above the declared maximum: ignored */
    TEST_ASSERT_EQUAL_UINT16(0xE9, hid_merge_get_consumer(&m));
}

void test_system_union_and_mask(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_set_system(&m, 0, HID_SYS_SLEEP);
    hid_merge_set_system(&m, 1, 0xFC | HID_SYS_WAKEUP);
    TEST_ASSERT_EQUAL_HEX8(HID_SYS_SLEEP | HID_SYS_WAKEUP, hid_merge_get_system(&m));
}

void test_mouse_sums_deltas_and_unions_buttons(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_mouse(&m, 0, HID_MOUSE_LEFT, 10, -3, 1, 0);
    hid_merge_mouse(&m, 1, HID_MOUSE_RIGHT | 0xE0, 5, 4, 0, -2);
    TEST_ASSERT_TRUE(hid_merge_mouse_pending(&m));
    hid_mouse_in_t r;
    TEST_ASSERT_TRUE(hid_merge_take_mouse(&m, &r));
    TEST_ASSERT_EQUAL_HEX8(HID_MOUSE_LEFT | HID_MOUSE_RIGHT, r.buttons);
    TEST_ASSERT_EQUAL_INT16(15, r.x);
    TEST_ASSERT_EQUAL_INT16(1, r.y);
    TEST_ASSERT_EQUAL_INT8(1, r.wheel);
    TEST_ASSERT_EQUAL_INT8(-2, r.pan);
    TEST_ASSERT_FALSE(hid_merge_mouse_pending(&m));
    /* Buttons persist after the movement was drained; deltas do not. */
    TEST_ASSERT_FALSE(hid_merge_take_mouse(&m, &r));
    TEST_ASSERT_EQUAL_HEX8(HID_MOUSE_LEFT | HID_MOUSE_RIGHT, r.buttons);
    TEST_ASSERT_EQUAL_INT16(0, r.x);
}

void test_mouse_opposite_motion_cancels(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_mouse(&m, 0, 0, 7, 0, 0, 0);
    hid_merge_mouse(&m, 1, 0, -7, 0, 0, 0);
    hid_mouse_in_t r;
    TEST_ASSERT_FALSE(hid_merge_take_mouse(&m, &r));
    TEST_ASSERT_FALSE(hid_merge_mouse_pending(&m));
}

void test_mouse_clamp_keeps_remainder(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_mouse(&m, 0, 0, 40000, 0, 300, 0);
    hid_mouse_in_t r;
    TEST_ASSERT_TRUE(hid_merge_take_mouse(&m, &r));
    TEST_ASSERT_EQUAL_INT16(32767, r.x);
    TEST_ASSERT_EQUAL_INT8(127, r.wheel);
    TEST_ASSERT_TRUE(hid_merge_mouse_pending(&m));
    TEST_ASSERT_TRUE(hid_merge_take_mouse(&m, &r));
    TEST_ASSERT_EQUAL_INT16(40000 - 32767, r.x);
    TEST_ASSERT_EQUAL_INT8(127, r.wheel);
    TEST_ASSERT_TRUE(hid_merge_take_mouse(&m, &r));
    TEST_ASSERT_EQUAL_INT8(300 - 254, r.wheel);
    TEST_ASSERT_FALSE(hid_merge_mouse_pending(&m));
}

void test_mouse_negative_clamp(void) {
    hid_merge_t m;
    hid_merge_init(&m);
    hid_merge_mouse(&m, 0, 0, -50000, 0, 0, 0);
    hid_mouse_in_t r;
    hid_merge_take_mouse(&m, &r);
    TEST_ASSERT_EQUAL_INT16(-32767, r.x);
    hid_merge_take_mouse(&m, &r);
    TEST_ASSERT_EQUAL_INT16(-(50000 - 32767), r.x);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_descriptors_well_formed);
    RUN_TEST(test_report_sizes);
    RUN_TEST(test_keyboard_union_and_dedupe);
    RUN_TEST(test_keyboard_rollover_error);
    RUN_TEST(test_keyboard_ignores_modifier_usages_and_zero);
    RUN_TEST(test_clear_source_releases_keys);
    RUN_TEST(test_out_of_range_source_is_ignored);
    RUN_TEST(test_consumer_first_source_wins_and_range);
    RUN_TEST(test_system_union_and_mask);
    RUN_TEST(test_mouse_sums_deltas_and_unions_buttons);
    RUN_TEST(test_mouse_opposite_motion_cancels);
    RUN_TEST(test_mouse_clamp_keeps_remainder);
    RUN_TEST(test_mouse_negative_clamp);
    return UNITY_END();
}
