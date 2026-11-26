/**
 * @file test_hid_parser.c
 * @brief Host tests for hid_parser and hid_normalizer: real-shaped report maps, edge cases and a garbage run.
 */
#include <stdlib.h>
#include <string.h>

#include "hid_normalizer.h"
#include "hid_parser.h"
#include "hid_report.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/* clang-format off */
/* A typical NKRO keyboard: report ID 1 = modifiers + 1 reserved + 6-key array; ID 2 = consumer 16-bit array;
 * ID 3 = a 104-bit key bitmap; LEDs in output report ID 1. Shaped like the maps of common BLE keyboards. */
static const uint8_t desc_kbd_ids[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x03,
    0x05, 0x07, 0x19, 0x04, 0x29, 0x6B, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x68, 0x81, 0x02,
    0xC0,
};

/* A mouse with 12-bit X/Y (packed), 8-bit wheel and AC Pan, no report IDs. */
static const uint8_t desc_mouse12[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95, 0x02, 0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
};
/* clang-format on */

void test_parse_boot_keyboard(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(hid_kbd_report_desc, HID_KBD_REPORT_DESC_LEN, &l));
    TEST_ASSERT_FALSE(l.uses_report_ids);
    TEST_ASSERT_EQUAL_UINT(8, hid_layout_report_bytes(&l, HID_REPORT_IN, 0));
    TEST_ASSERT_EQUAL_UINT(1, hid_layout_report_bytes(&l, HID_REPORT_OUT, 0));
}

void test_parse_dongle_ctrl_descriptor(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(hid_ctrl_report_desc, HID_CTRL_REPORT_DESC_LEN, &l));
    TEST_ASSERT_TRUE(l.uses_report_ids);
    TEST_ASSERT_EQUAL_UINT(HID_MOUSE_REPORT_SIZE, hid_layout_report_bytes(&l, HID_REPORT_IN, HID_RID_MOUSE));
    TEST_ASSERT_EQUAL_UINT(2, hid_layout_report_bytes(&l, HID_REPORT_IN, HID_RID_CONSUMER));
    TEST_ASSERT_EQUAL_UINT(1, hid_layout_report_bytes(&l, HID_REPORT_IN, HID_RID_SYSTEM));
    TEST_ASSERT_EQUAL_UINT(sizeof(hid_tp_in_t), hid_layout_report_bytes(&l, HID_REPORT_IN, HID_RID_TOUCHPAD));
}

void test_decode_boot_keyboard(void) {
    hid_layout_t l;
    hid_norm_boot_keyboard_layout(&l);
    const uint8_t rep[8] = {0x22, 0, 0x04, 0x05, 0, 0, 0, 0};
    hid_norm_event_t ev;
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 0, rep, sizeof(rep), &ev));
    TEST_ASSERT_TRUE(ev.has_keyboard);
    TEST_ASSERT_EQUAL_HEX8(0x22, ev.modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x04, ev.keys[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05, ev.keys[1]);
    TEST_ASSERT_EQUAL_HEX8(0, ev.keys[2]);
}

void test_decode_rollover_error_ignored(void) {
    hid_layout_t l;
    hid_norm_boot_keyboard_layout(&l);
    const uint8_t rep[8] = {0, 0, 1, 1, 1, 1, 1, 1};
    hid_norm_event_t ev;
    hid_norm_decode(&l, 0, rep, sizeof(rep), &ev);
    TEST_ASSERT_EQUAL_HEX8(0, ev.keys[0]);
}

void test_decode_short_report_is_safe(void) {
    hid_layout_t l;
    hid_norm_boot_keyboard_layout(&l);
    const uint8_t rep[3] = {0x01, 0, 0x04};
    hid_norm_event_t ev;
    hid_norm_decode(&l, 0, rep, sizeof(rep), &ev); /* array field cut short: must not read past the buffer */
    TEST_ASSERT_EQUAL_HEX8(0x01, ev.modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x04, ev.keys[0]);
}

void test_ids_keyboard_consumer_bitmap(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc_kbd_ids, sizeof(desc_kbd_ids), &l));
    TEST_ASSERT_TRUE(l.uses_report_ids);
    TEST_ASSERT_EQUAL_UINT(8, hid_layout_report_bytes(&l, HID_REPORT_IN, 1));
    TEST_ASSERT_EQUAL_UINT(2, hid_layout_report_bytes(&l, HID_REPORT_IN, 2));
    TEST_ASSERT_EQUAL_UINT(13, hid_layout_report_bytes(&l, HID_REPORT_IN, 3));

    hid_norm_event_t ev;
    const uint8_t k[8] = {0x01, 0, 0x2C, 0, 0, 0, 0, 0}; /* LCtrl + Space */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 1, k, sizeof(k), &ev));
    TEST_ASSERT_EQUAL_HEX8(0x01, ev.modifiers);
    TEST_ASSERT_EQUAL_HEX8(0x2C, ev.keys[0]);

    const uint8_t c[2] = {0xE9, 0x00}; /* Volume Up */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 2, c, sizeof(c), &ev));
    TEST_ASSERT_TRUE(ev.has_consumer);
    TEST_ASSERT_EQUAL_UINT16(0xE9, ev.consumer);
    TEST_ASSERT_FALSE(ev.has_keyboard);

    uint8_t bm[13] = {0};
    bm[0] = 0x01; /* usage 0x04 = 'a' */
    bm[1] = 0x80; /* usage 0x04 + 15 = 0x13 */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 3, bm, sizeof(bm), &ev));
    TEST_ASSERT_EQUAL_HEX8(0x04, ev.keys[0]);
    TEST_ASSERT_EQUAL_HEX8(0x13, ev.keys[1]);
    TEST_ASSERT_EQUAL_HEX8(0, ev.keys[2]);

    /* Unknown report ID: nothing decoded. */
    TEST_ASSERT_FALSE(hid_norm_decode(&l, 9, k, sizeof(k), &ev));
}

void test_led_report(void) {
    hid_layout_t l;
    hid_parser_parse(desc_kbd_ids, sizeof(desc_kbd_ids), &l);
    uint8_t id, out[4];
    size_t n = hid_norm_build_led_report(&l, HID_LED_CAPS_LOCK | HID_LED_SCROLL_LOCK, &id, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT8(1, id);
    TEST_ASSERT_EQUAL_HEX8(0x06, out[0]);
    /* Buffer too small -> 0. */
    TEST_ASSERT_EQUAL_UINT(0, hid_norm_build_led_report(&l, 1, &id, out, 0));
    /* A device without LED output. */
    hid_parser_parse(desc_mouse12, sizeof(desc_mouse12), &l);
    TEST_ASSERT_EQUAL_UINT(0, hid_norm_build_led_report(&l, 1, &id, out, sizeof(out)));
}

void test_mouse_12bit_packed(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc_mouse12, sizeof(desc_mouse12), &l));
    TEST_ASSERT_FALSE(l.uses_report_ids);
    TEST_ASSERT_EQUAL_UINT(1 + 3 + 1 + 1, hid_layout_report_bytes(&l, HID_REPORT_IN, 0));
    /* buttons=left+right, X=-2 (0xFFE), Y=+300 (0x12C), wheel=-1, pan=+2 */
    const uint8_t rep[] = {0x03, 0xFE, 0xCF, 0x12, 0xFF, 0x02};
    hid_norm_event_t ev;
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 0, rep, sizeof(rep), &ev));
    TEST_ASSERT_TRUE(ev.has_mouse);
    TEST_ASSERT_EQUAL_HEX8(0x03, ev.buttons);
    TEST_ASSERT_EQUAL_INT32(-2, ev.dx);
    TEST_ASSERT_EQUAL_INT32(300, ev.dy);
    TEST_ASSERT_EQUAL_INT32(-1, ev.wheel);
    TEST_ASSERT_EQUAL_INT32(2, ev.pan);
}

void test_dongle_mouse_and_system_roundtrip(void) {
    hid_layout_t l;
    hid_parser_parse(hid_ctrl_report_desc, HID_CTRL_REPORT_DESC_LEN, &l);
    hid_mouse_in_t m = {.buttons = HID_MOUSE_MIDDLE, .x = -1000, .y = 7, .wheel = 3, .pan = -4};
    hid_norm_event_t ev;
    TEST_ASSERT_TRUE(hid_norm_decode(&l, HID_RID_MOUSE, (const uint8_t *)&m, sizeof(m), &ev));
    TEST_ASSERT_EQUAL_HEX8(HID_MOUSE_MIDDLE, ev.buttons);
    TEST_ASSERT_EQUAL_INT32(-1000, ev.dx);
    TEST_ASSERT_EQUAL_INT32(7, ev.dy);
    TEST_ASSERT_EQUAL_INT32(3, ev.wheel);
    TEST_ASSERT_EQUAL_INT32(-4, ev.pan);
    const uint8_t sys = HID_SYS_WAKEUP;
    TEST_ASSERT_TRUE(hid_norm_decode(&l, HID_RID_SYSTEM, &sys, 1, &ev));
    TEST_ASSERT_EQUAL_HEX8(HID_SYS_WAKEUP, ev.system);
}

void test_truncated_descriptor(void) {
    hid_layout_t l;
    hid_parse_result_t r = hid_parser_parse(hid_kbd_report_desc, 20, &l);
    TEST_ASSERT_TRUE(r == HID_PARSE_TRUNCATED || r == HID_PARSE_BAD_DEPTH);
    TEST_ASSERT_EQUAL(HID_PARSE_INVALID, hid_parser_parse(NULL, 5, &l));
    TEST_ASSERT_EQUAL(HID_PARSE_INVALID, hid_parser_parse(hid_kbd_report_desc, 0, &l));
    TEST_ASSERT_EQUAL(HID_PARSE_INVALID, hid_parser_parse(hid_kbd_report_desc, 5, NULL));
}

void test_unbalanced_and_too_deep(void) {
    hid_layout_t l;
    const uint8_t end_only[] = {0xC0};
    TEST_ASSERT_EQUAL(HID_PARSE_BAD_DEPTH, hid_parser_parse(end_only, 1, &l));
    uint8_t deep[40];
    for (int i = 0; i < 40; i++) {
        deep[i] = 0xA1; /* Collection with size 1: swallows the next byte, so use 2-byte items */
    }
    uint8_t deep2[80];
    for (int i = 0; i < 40; i++) {
        deep2[2 * i] = 0xA1;
        deep2[2 * i + 1] = 0x01;
    }
    TEST_ASSERT_EQUAL(HID_PARSE_BAD_DEPTH, hid_parser_parse(deep2, sizeof(deep2), &l));
    (void)deep;
}

void test_long_item_skipped(void) {
    hid_layout_t l;
    const uint8_t d[] = {0xFE, 0x02, 0x10, 0xAA, 0xBB, 0x05, 0x01};
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(d, sizeof(d), &l));
    const uint8_t bad[] = {0xFE, 0x20, 0x10, 0xAA};
    TEST_ASSERT_EQUAL(HID_PARSE_TRUNCATED, hid_parser_parse(bad, sizeof(bad), &l));
}

void test_field_capacity_is_bounded(void) {
    /* 100 separate one-bit variable inputs: the parser must stop at its capacity without overflow. */
    uint8_t d[8 + 100 * 6];
    size_t n = 0;
    d[n++] = 0x05;
    d[n++] = 0x09;
    d[n++] = 0x15;
    d[n++] = 0x00;
    d[n++] = 0x25;
    d[n++] = 0x01;
    d[n++] = 0x75;
    d[n++] = 0x01;
    for (int i = 0; i < 100; i++) {
        d[n++] = 0x95;
        d[n++] = 0x01;
        d[n++] = 0x09;
        d[n++] = (uint8_t)(i + 1);
        d[n++] = 0x81;
        d[n++] = 0x02;
    }
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_TOO_MANY, hid_parser_parse(d, n, &l));
    TEST_ASSERT_EQUAL_UINT(HID_PARSER_MAX_FIELDS, l.nfields);
}

void test_zero_and_huge_sizes_ignored(void) {
    hid_layout_t l;
    const uint8_t d[] = {0x05, 0x07, 0x75, 0x00, 0x95, 0x08, 0x81, 0x02, 0x75, 0x40,
                         0x81, 0x02, 0x75, 0x08, 0x96, 0xFF, 0xFF, 0x81, 0x02};
    hid_parser_parse(d, sizeof(d), &l);
    TEST_ASSERT_EQUAL_UINT(0, l.nfields);
}

void test_push_pop(void) {
    hid_layout_t l;
    const uint8_t d[] = {0x05, 0x07, 0x75, 0x08, 0x95, 0x01, 0xA4, 0x75, 0x01, 0x95, 0x08, 0x19,
                         0xE0, 0x29, 0xE7, 0x81, 0x02, 0xB4, 0x19, 0x04, 0x29, 0x04, 0x81, 0x00};
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(d, sizeof(d), &l));
    TEST_ASSERT_EQUAL_UINT(2, l.nfields);
    TEST_ASSERT_EQUAL_UINT8(1, l.fields[0].bit_size);
    TEST_ASSERT_EQUAL_UINT8(8, l.fields[1].bit_size);
    const uint8_t pop_empty[] = {0xB4};
    TEST_ASSERT_EQUAL(HID_PARSE_BAD_DEPTH, hid_parser_parse(pop_empty, 1, &l));
}

/** Deterministic xorshift so the garbage run is reproducible. */
static uint32_t rng_state = 0x1234ABCDu;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void test_garbage_never_crashes(void) {
    uint8_t buf[200];
    uint8_t rep[64];
    hid_layout_t l;
    hid_norm_event_t ev;
    for (int iter = 0; iter < 20000; iter++) {
        size_t n = rnd() % sizeof(buf);
        for (size_t i = 0; i < n; i++) {
            buf[i] = (uint8_t)rnd();
        }
        /* Bias towards valid-looking item prefixes by mutating a real descriptor half of the time. */
        if (iter & 1) {
            size_t m = HID_KBD_REPORT_DESC_LEN < sizeof(buf) ? HID_KBD_REPORT_DESC_LEN : sizeof(buf);
            memcpy(buf, hid_kbd_report_desc, m);
            for (int k = 0; k < 4; k++) {
                buf[rnd() % m] = (uint8_t)rnd();
            }
            n = m;
        }
        hid_parser_parse(buf, n, &l);
        for (size_t i = 0; i < sizeof(rep); i++) {
            rep[i] = (uint8_t)rnd();
        }
        size_t rl = rnd() % sizeof(rep);
        hid_norm_decode(&l, (uint8_t)(rnd() % 4), rep, rl, &ev);
        uint8_t id, out[16];
        hid_norm_build_led_report(&l, 7, &id, out, sizeof(out));
    }
}

/** Regression (found by the fuzzer): several 32-bit X fields summed with signed overflow. Now saturates. */
void test_axis_sum_saturates(void) {
    static const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,             /* Generic Desktop, Mouse, Collection (Application) */
        0x17, 0x00, 0x00, 0x00, 0x80,                   /* Logical Minimum -2^31 */
        0x27, 0xFF, 0xFF, 0xFF, 0x7F,                   /* Logical Maximum 2^31-1 */
        0x75, 0x20, 0x95, 0x01,                         /* Report Size 32, Count 1 */
        0x09, 0x30, 0x81, 0x06,                         /* Usage X, Input (Data, Variable, Relative) */
        0x09, 0x30, 0x81, 0x06,                         /* a second X field */
        0x09, 0x31, 0x81, 0x06, 0x09, 0x31, 0x81, 0x06, /* two Y fields */
        0xC0,
    };
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc, sizeof(desc), &l));
    const uint8_t big[16] = {0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F,
                             0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80};
    hid_norm_event_t ev;
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 0, big, sizeof(big), &ev));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, ev.dx);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, ev.dy);
}

/** Regression: a trackball that also declares a keyboard collection is still a pointer (gets the click challenge). */
void test_is_pointer_with_keyboard_collection(void) {
    static const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, /* Mouse, report ID 1 */
        0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0, 0x05, 0x01,
        0x09, 0x06, 0xA1, 0x01, 0x85, 0x02, /* Keyboard, report ID 2 */
        0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0,
    };
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc, sizeof(desc), &l));
    TEST_ASSERT_TRUE(hid_norm_is_pointer(&l));
    hid_layout_t kb;
    hid_norm_boot_keyboard_layout(&kb);
    TEST_ASSERT_FALSE(hid_norm_is_pointer(&kb));
}

/* A keyboard with a mouse collection (some keyboards expose one) is still a keyboard; a mouse with shortcut keys is
 * not. */
static const uint8_t KBD_WITH_MOUSE[] =
    {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, /* Keyboard, ID 1 */
        0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, /* modifiers */
        0x95, 0x01, 0x75, 0x08, 0x81, 0x01, /* reserved byte */
        0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00,
        0x81, 0x00,                                                                   /* keys */
        0xC0, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, /* Mouse, ID 2 */
        0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
        0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81,
        0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
};
static const uint8_t MOUSE_WITH_SHORTCUT_KEYS[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, /* Mouse, ID 1 */
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95,
    0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0, 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02, /* Keyboard-ish
                                                                                                       collection, ID 2:
                                                                                                       key array only */
    0x05, 0x07, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
};

/* A trackball whose keyboard collection has modifier keys too (as the ProtoArc EM06 seems to): still a pointer. */
static const uint8_t MOUSE_WITH_MODIFIER_KEYBOARD[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, /* Mouse first, ID 1 */
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95,
    0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0, 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02, /* Keyboard, ID 2, WITH
                                                                                           modifiers */
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95,
    0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
};

void test_trackball_with_modifier_keyboard_is_not_a_keyboard(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK,
                      hid_parser_parse(MOUSE_WITH_MODIFIER_KEYBOARD, sizeof(MOUSE_WITH_MODIFIER_KEYBOARD), &l));
    TEST_ASSERT_EQUAL_UINT16(0x01, l.primary_page);
    TEST_ASSERT_EQUAL_UINT16(0x02, l.primary_usage); /* mouse first */
    TEST_ASSERT_TRUE(hid_norm_is_pointer(&l));
    TEST_ASSERT_FALSE(hid_norm_is_keyboard(&l));
    /* ... so it can neither be offered the typed challenge nor count as "an already paired keyboard". */
    TEST_ASSERT_FALSE(hid_norm_pairing_is_keyboard(0, &l));
}

void test_primary_collection_of_a_keyboard(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(KBD_WITH_MOUSE, sizeof(KBD_WITH_MOUSE), &l));
    TEST_ASSERT_EQUAL_UINT16(0x06, l.primary_usage);
}

void test_keyboard_with_mouse_is_a_keyboard(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(KBD_WITH_MOUSE, sizeof(KBD_WITH_MOUSE), &l));
    TEST_ASSERT_TRUE(hid_norm_is_pointer(&l));
    TEST_ASSERT_TRUE(hid_norm_is_keyboard(&l));
    TEST_ASSERT_TRUE(hid_norm_pairing_is_keyboard(0, &l));
}

void test_mouse_with_shortcut_keys_is_not_a_keyboard(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(MOUSE_WITH_SHORTCUT_KEYS, sizeof(MOUSE_WITH_SHORTCUT_KEYS), &l));
    TEST_ASSERT_TRUE(hid_norm_is_pointer(&l));
    TEST_ASSERT_FALSE(hid_norm_is_keyboard(&l));
    TEST_ASSERT_FALSE(hid_norm_pairing_is_keyboard(0, &l));
}

void test_appearance_overrides_the_report_map(void) {
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(MOUSE_WITH_SHORTCUT_KEYS, sizeof(MOUSE_WITH_SHORTCUT_KEYS), &l));
    TEST_ASSERT_TRUE(hid_norm_pairing_is_keyboard(0x03C1, &l)); /* advertises "keyboard" */
    hid_layout_t k;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(KBD_WITH_MOUSE, sizeof(KBD_WITH_MOUSE), &k));
    TEST_ASSERT_FALSE(hid_norm_pairing_is_keyboard(0x03C2, &k)); /* advertises "mouse" */
    hid_layout_t boot;
    hid_norm_boot_keyboard_layout(&boot);
    TEST_ASSERT_TRUE(hid_norm_is_keyboard(&boot));
}

/* The notification payload has no report ID byte; the ID comes from the Report Reference descriptor. */
void test_decode_realistic_mouse_and_keyboard_notifications(void) {
    hid_layout_t l;
    hid_norm_event_t ev;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(MOUSE_WITH_SHORTCUT_KEYS, sizeof(MOUSE_WITH_SHORTCUT_KEYS), &l));
    const uint8_t click[] = {0x02, 0x00, 0x00, 0x00, 0x00}; /* right button, no motion */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 1, click, sizeof(click), &ev));
    TEST_ASSERT_TRUE(ev.has_mouse);
    TEST_ASSERT_EQUAL_UINT8(0x02, ev.buttons);
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(KBD_WITH_MOUSE, sizeof(KBD_WITH_MOUSE), &l));
    const uint8_t keys[] = {0x02, 0x00, 0x04, 0, 0, 0, 0, 0}; /* LShift + A */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 1, keys, sizeof(keys), &ev));
    TEST_ASSERT_TRUE(ev.has_keyboard);
    TEST_ASSERT_EQUAL_UINT8(0x02, ev.modifiers);
    TEST_ASSERT_EQUAL_UINT8(0x04, ev.keys[0]);
    const uint8_t motion[] = {0x01, 0x05, 0xFB, 0x00};
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 2, motion, sizeof(motion), &ev));
    TEST_ASSERT_EQUAL_INT32(5, ev.dx);
    TEST_ASSERT_EQUAL_INT32(-5, ev.dy);
}

/** Touchpad reporting absolute X/Y (Data,Var,Abs): positions must become movement, not pin the pointer. */
void test_absolute_pointer_becomes_relative(void) {
    static const uint8_t desc[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x09, 0x30,
                                   0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02, 0x81,
                                   0x02, 0xC0, 0xC0};
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc, sizeof(desc), &l));
    hid_abs_state_t st = {0};
    hid_norm_event_t ev;
    const uint8_t a[4] = {0x58, 0x04, 0x40, 0x06}; /* 1112, 1600 */
    const uint8_t b[4] = {0x62, 0x04, 0x38, 0x06}; /* 1122, 1592 */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 0, a, sizeof(a), &ev));
    TEST_ASSERT_TRUE(ev.abs_xy);
    hid_norm_abs_to_rel(&st, &ev, 1000); /* first contact: no jump */
    TEST_ASSERT_EQUAL_INT32(0, ev.dx);
    TEST_ASSERT_EQUAL_INT32(0, ev.dy);
    hid_norm_decode(&l, 0, b, sizeof(b), &ev);
    hid_norm_abs_to_rel(&st, &ev, 1008);
    TEST_ASSERT_EQUAL_INT32(10, ev.dx);
    TEST_ASSERT_EQUAL_INT32(-8, ev.dy);
    hid_norm_decode(&l, 0, a, sizeof(a), &ev);
    hid_norm_abs_to_rel(&st, &ev, 1008 + HID_ABS_TOUCH_GAP_MS + 1); /* finger lifted and put down elsewhere */
    TEST_ASSERT_EQUAL_INT32(0, ev.dx);
    TEST_ASSERT_EQUAL_INT32(0, ev.dy);
}

/** Touchpad sending relative X/Y plus absolute X/Y in one report: the relative axes move, the positions are ignored. */
void test_relative_wins_over_absolute(void) {
    static const uint8_t desc[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25,
                                   0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, /* relative X, Y (int8) */
                                   0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02,
                                   0x81, 0x02, /* absolute X, Y (0..4095) */
                                   0xC0};
    hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc, sizeof(desc), &l));
    hid_abs_state_t st = {0};
    hid_norm_event_t ev;
    const uint8_t r[6] = {0x05, 0xFD, 0x6C, 0x04, 0x30, 0x06}; /* +5, -3, at 1132, 1584 */
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 0, r, sizeof(r), &ev));
    hid_norm_abs_to_rel(&st, &ev, 1000);
    TEST_ASSERT_EQUAL_INT32(5, ev.dx);
    TEST_ASSERT_EQUAL_INT32(-3, ev.dy);
    hid_norm_decode(&l, 0, r, sizeof(r), &ev);
    hid_norm_abs_to_rel(&st, &ev, 1008);
    TEST_ASSERT_EQUAL_INT32(5, ev.dx);
}

/** Inateck keyboard touchpad report map: 4 finger slots (Tip Switch, Contact ID, absolute X/Y) and 2 buttons. */
static const uint8_t k_inateck_map[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x15, 0x00, 0x25, 0x01, 0x05, 0x07, 0x1A, 0xE0, 0x00, 0x2A, 0xE7, 0x00, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x05, 0x07, 0x19, 0x00, 0x2A, 0x91, 0x00, 0x16, 0x00, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x0A, 0x81, 0x00, 0x05, 0x08, 0x75, 0x01, 0x95, 0x03, 0x19, 0x01, 0x29, 0x03, 0x25, 0x01, 0x91, 0x02, 0x95, 0x05, 0x91, 0x01, 0xC0, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x0C, 0x95, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x81, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x01, 0xC0, 0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x06, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x25, 0x07, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x03, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0x80, 0x0C, 0x75, 0x0C, 0x55, 0x0E, 0x65, 0x11, 0x09, 0x30, 0x35, 0x00, 0x46, 0x99, 0x03, 0x95, 0x01, 0x81, 0x02, 0x26, 0x0A, 0x08, 0x46, 0xFA, 0x01, 0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x25, 0x07, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x03, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0x80, 0x0C, 0x75, 0x0C, 0x55, 0x0E, 0x65, 0x11, 0x09, 0x30, 0x35, 0x00, 0x46, 0xE8, 0x03, 0x95, 0x01, 0x81, 0x02, 0x26, 0x0A, 0x08, 0x46, 0xF4, 0x01, 0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x25, 0x07, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x03, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0x80, 0x0C, 0x75, 0x0C, 0x55, 0x0E, 0x65, 0x11, 0x09, 0x30, 0x35, 0x00, 0x46, 0xE8, 0x03, 0x95, 0x01, 0x81, 0x02, 0x26, 0x0A, 0x08, 0x46, 0xF4, 0x01, 0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x25, 0x07, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x03, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0x80, 0x0C, 0x75, 0x0C, 0x55, 0x0E, 0x65, 0x11, 0x09, 0x30, 0x35, 0x00, 0x46, 0xE8, 0x03, 0x95, 0x01, 0x81, 0x02, 0x26, 0x0A, 0x08, 0x46, 0xF4, 0x01, 0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x09, 0x25, 0x01, 0x19, 0x01, 0x29, 0x02, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x16, 0x81, 0x01, 0xC0};

/** Feed one Inateck report (ID 6); the event is left in @p ev. */
static void feed(const hid_layout_t *l, hid_abs_state_t *st, const uint8_t *r, size_t n, uint32_t ms,
                 hid_norm_event_t *ev) {
    TEST_ASSERT_TRUE(hid_norm_decode(l, 6, r, n, ev));
    hid_norm_abs_to_rel(st, ev, ms);
}
#define FEED(r, ms) feed(&l, &st, r, sizeof(r), ms, &ev)

/** Movement follows the touching finger slot; a lift and a new touch elsewhere do not jump. */
void test_touchpad_follows_finger(void) {
    static hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(k_inateck_map, sizeof(k_inateck_map), &l));
    const uint8_t a[] = {0x01, 0x00, 0x00, 0x00, 0x07, 0xE8, 0x43, 0x1F, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; /* slot 1 */
    const uint8_t b[] = {0x01, 0x00, 0x00, 0x00, 0x07, 0xF2, 0xF3, 0x1E, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t up[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t c[] = {0x0B, 0xD0, 0xC7, 0x5D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; /* new touch in slot 0 */
    hid_abs_state_t st = {0};
    hid_norm_event_t ev;
    FEED(a, 1000);
    TEST_ASSERT_EQUAL_INT32(0, ev.dx);
    FEED(b, 1008);
    TEST_ASSERT_EQUAL_INT32(10, ev.dx);
    TEST_ASSERT_EQUAL_INT32(-5, ev.dy);
    FEED(up, 1016);
    FEED(c, 1024);
    TEST_ASSERT_EQUAL_INT32(0, ev.dx);
    TEST_ASSERT_EQUAL_INT32(0, ev.dy);
}

/** Two fingers: the one that moves gives the movement; a lone finger whose contact ID changes stays tracked. */
void test_touchpad_moving_finger_and_id_change(void) {
    static hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(k_inateck_map, sizeof(k_inateck_map), &l));
    const uint8_t p1[] = {0x03, 0xF4, 0x81, 0x70, 0x07, 0xDC, 0x05, 0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    const uint8_t p2[] = {0x03, 0xF5, 0x81, 0x70, 0x07, 0xA4, 0xE6, 0x2E, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    const uint8_t q1[] = {0x0F, 0xE8, 0x43, 0x1F, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t q2[] = {0x13, 0xB0, 0x44, 0x1F, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; /* same finger, new contact ID */
    const uint8_t up[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    hid_abs_state_t st = {0};
    hid_norm_event_t ev;
    FEED(p1, 1000);
    FEED(p2, 1008);
    TEST_ASSERT_EQUAL_INT32(200, ev.dx);
    TEST_ASSERT_EQUAL_INT32(-50, ev.dy);
    TEST_ASSERT_EQUAL_HEX8(1, ev.buttons);
    FEED(up, 1016);
    FEED(q1, 2000);
    FEED(q2, 2008);
    TEST_ASSERT_EQUAL_INT32(200, ev.dx);
}

/** Contacts carry Contact ID and Confidence (a palm the pad flags is not confident). */
void test_touchpad_contacts_and_confidence(void) {
    static hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(k_inateck_map, sizeof(k_inateck_map), &l));
    const uint8_t r[] = {0x0B, 0x64, 0x80, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x16, 0xB8, 0x0B, 0x7D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    hid_norm_event_t ev;
    TEST_ASSERT_TRUE(hid_norm_decode(&l, 6, r, sizeof(r), &ev));
    TEST_ASSERT_TRUE(ev.touch_report);
    TEST_ASSERT_EQUAL_UINT(2, ev.ncontacts);
    TEST_ASSERT_EQUAL_INT32(2, ev.contacts[0].id);
    TEST_ASSERT_EQUAL_INT32(100, ev.contacts[0].x);
    TEST_ASSERT_EQUAL_INT32(200, ev.contacts[0].y);
    TEST_ASSERT_TRUE(ev.contacts[0].confident);
    TEST_ASSERT_EQUAL_INT32(5, ev.contacts[1].id);
    TEST_ASSERT_EQUAL_INT32(3000, ev.contacts[1].x);
    TEST_ASSERT_FALSE(ev.contacts[1].confident);
}

/** Geometry from the report map, with the Inateck's wrong physical size corrected to the measured one. */
void test_touchpad_geometry_inateck_fix(void) {
    static hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(k_inateck_map, sizeof(k_inateck_map), &l));
    hid_tp_geometry_t g;
    int32_t x0, y0;
    TEST_ASSERT_TRUE(hid_norm_touchpad_geometry(&l, &g, &x0, &y0));
    TEST_ASSERT_EQUAL_UINT16(3200, g.x_max);
    TEST_ASSERT_EQUAL_UINT16(2058, g.y_max);
    TEST_ASSERT_EQUAL_UINT16(1260, g.x_mm10); /* reported 921 */
    TEST_ASSERT_EQUAL_UINT16(820, g.y_mm10);  /* reported 506 */
    TEST_ASSERT_EQUAL_INT32(0, x0);
    hid_tp_geometry_t d = HID_TP_DEFAULT_GEOMETRY;
    TEST_ASSERT_EQUAL_MEMORY(&d, &g, sizeof(g)); /* the default descriptor is right for it: no re-enumeration */
}

/** Geometry of another pad: size in inches with exponent -3, X range not starting at 0; no fingers: no geometry. */
void test_touchpad_geometry_inch_and_none(void) {
    static const uint8_t desc[] = {
        0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x22, 0xA1, 0x02,
        0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, /* Tip Switch */
        0x75, 0x07, 0x81, 0x03,
        0x05, 0x01, 0x55, 0x0D, 0x65, 0x13, 0x75, 0x10,                         /* exp -3, inch */
        0x16, 0x64, 0x00, 0x26, 0x74, 0x0E, 0x35, 0x00, 0x46, 0xB8, 0x0B,       /* X 100..3700, 3.000 in */
        0x09, 0x30, 0x81, 0x02,
        0x15, 0x00, 0x26, 0xD0, 0x07, 0x46, 0xD0, 0x07,                         /* Y 0..2000, 2.000 in */
        0x09, 0x31, 0x81, 0x02, 0xC0, 0xC0,
    };
    static hid_layout_t l;
    TEST_ASSERT_EQUAL(HID_PARSE_OK, hid_parser_parse(desc, sizeof(desc), &l));
    hid_tp_geometry_t g;
    int32_t x0, y0;
    TEST_ASSERT_TRUE(hid_norm_touchpad_geometry(&l, &g, &x0, &y0));
    TEST_ASSERT_EQUAL_UINT16(3600, g.x_max);
    TEST_ASSERT_EQUAL_UINT16(2000, g.y_max);
    TEST_ASSERT_EQUAL_UINT16(762, g.x_mm10); /* 3 in = 76.2 mm */
    TEST_ASSERT_EQUAL_UINT16(508, g.y_mm10);
    TEST_ASSERT_EQUAL_INT32(100, x0);
    hid_layout_t kb;
    hid_norm_boot_keyboard_layout(&kb);
    TEST_ASSERT_FALSE(hid_norm_touchpad_geometry(&kb, &g, &x0, &y0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_boot_keyboard);
    RUN_TEST(test_parse_dongle_ctrl_descriptor);
    RUN_TEST(test_decode_boot_keyboard);
    RUN_TEST(test_decode_rollover_error_ignored);
    RUN_TEST(test_decode_short_report_is_safe);
    RUN_TEST(test_ids_keyboard_consumer_bitmap);
    RUN_TEST(test_led_report);
    RUN_TEST(test_mouse_12bit_packed);
    RUN_TEST(test_dongle_mouse_and_system_roundtrip);
    RUN_TEST(test_truncated_descriptor);
    RUN_TEST(test_unbalanced_and_too_deep);
    RUN_TEST(test_long_item_skipped);
    RUN_TEST(test_field_capacity_is_bounded);
    RUN_TEST(test_zero_and_huge_sizes_ignored);
    RUN_TEST(test_push_pop);
    RUN_TEST(test_garbage_never_crashes);
    RUN_TEST(test_axis_sum_saturates);
    RUN_TEST(test_is_pointer_with_keyboard_collection);
    RUN_TEST(test_trackball_with_modifier_keyboard_is_not_a_keyboard);
    RUN_TEST(test_primary_collection_of_a_keyboard);
    RUN_TEST(test_keyboard_with_mouse_is_a_keyboard);
    RUN_TEST(test_mouse_with_shortcut_keys_is_not_a_keyboard);
    RUN_TEST(test_appearance_overrides_the_report_map);
    RUN_TEST(test_decode_realistic_mouse_and_keyboard_notifications);
    RUN_TEST(test_absolute_pointer_becomes_relative);
    RUN_TEST(test_relative_wins_over_absolute);
    RUN_TEST(test_touchpad_follows_finger);
    RUN_TEST(test_touchpad_moving_finger_and_id_change);
    RUN_TEST(test_touchpad_contacts_and_confidence);
    RUN_TEST(test_touchpad_geometry_inateck_fix);
    RUN_TEST(test_touchpad_geometry_inch_and_none);
    return UNITY_END();
}
