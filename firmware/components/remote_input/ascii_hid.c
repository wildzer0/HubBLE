/**
 * @file ascii_hid.c
 * @brief ASCII -> HID key press, US layout. See ascii_hid.h.
 *
 * Dependencies: libc only. Threading: stateless.
 */
#include "ascii_hid.h"

#include <stddef.h>

/** Usage of 'a'; the letters are consecutive (HID Usage Tables 1.4, Keyboard/Keypad page). */
#define USAGE_A 0x04
/** Usage of '1'; 1..9 are consecutive and '0' follows them. */
#define USAGE_1 0x1E
#define USAGE_0 0x27

/** A punctuation key: its usage and the characters it types without and with Shift. */
typedef struct {
    uint8_t usage; /**< Key usage. */
    char plain;    /**< Character without Shift. */
    char shifted;  /**< Character with Shift. */
} punct_t;

/** Keys of the US layout that are neither letters nor digits (nor space, Enter and Tab, handled separately). */
static const punct_t PUNCT[] = {
    {0x2D, '-', '_'},  {0x2E, '=', '+'}, {0x2F, '[', '{'}, {0x30, ']', '}'}, {0x31, '\\', '|'}, {0x33, ';', ':'},
    {0x34, '\'', '"'}, {0x35, '`', '~'}, {0x36, ',', '<'}, {0x37, '.', '>'}, {0x38, '/', '?'},
};

/** Characters typed by Shift + digit, in the order 1..9, 0. */
static const char SHIFTED_DIGITS[] = "!@#$%^&*()";

/** @copydoc ascii_hid_lookup */
bool ascii_hid_lookup(char c, ascii_hid_key_t *out) {
    ascii_hid_key_t k = {0, 0};
    if (c >= 'a' && c <= 'z') {
        k.usage = (uint8_t)(USAGE_A + (c - 'a'));
    } else if (c >= 'A' && c <= 'Z') {
        k.usage = (uint8_t)(USAGE_A + (c - 'A'));
        k.modifiers = ASCII_HID_SHIFT;
    } else if (c >= '1' && c <= '9') {
        k.usage = (uint8_t)(USAGE_1 + (c - '1'));
    } else if (c == '0') {
        k.usage = USAGE_0;
    } else if (c == ' ') {
        k.usage = 0x2C;
    } else if (c == '\n') {
        k.usage = 0x28;
    } else if (c == '\t') {
        k.usage = 0x2B;
    } else {
        for (size_t i = 0; SHIFTED_DIGITS[i] != '\0'; i++) {
            if (SHIFTED_DIGITS[i] == c) {
                k.usage = (uint8_t)(i == 9 ? USAGE_0 : USAGE_1 + i);
                k.modifiers = ASCII_HID_SHIFT;
                *out = k;
                return true;
            }
        }
        for (size_t i = 0; i < sizeof(PUNCT) / sizeof(PUNCT[0]); i++) {
            if (PUNCT[i].plain == c || PUNCT[i].shifted == c) {
                k.usage = PUNCT[i].usage;
                k.modifiers = PUNCT[i].shifted == c ? ASCII_HID_SHIFT : 0;
                *out = k;
                return true;
            }
        }
        return false;
    }
    *out = k;
    return true;
}
