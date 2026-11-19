/**
 * @file ascii_hid.h
 * @brief Pure mapping from printable ASCII to a USB HID key press (US layout), for typing text from the phone.
 *
 * The host turns key usages into characters with ITS keyboard layout, so the text arrives as typed only when the
 * host uses a US layout; letters and digits are the same on most layouts, punctuation is not (docs/user-guide.md).
 *
 * Dependencies: libc only (host-testable). Threading: stateless.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Left Shift modifier bit of the HID modifier byte. */
#define ASCII_HID_SHIFT 0x02

/** One key press that produces a character. */
typedef struct {
    uint8_t usage;     /**< Key usage (page 0x07). */
    uint8_t modifiers; /**< Modifier byte to hold with it (0 or ASCII_HID_SHIFT). */
} ascii_hid_key_t;

/**
 * @brief Look up the key press that types a character on a US keyboard.
 *
 * @param c   Character: printable ASCII, '\n' (Enter) or '\t' (Tab).
 * @param out Receives the key press.
 * @return false if the character has no key (non-ASCII or other control characters); @p out is then untouched.
 */
bool ascii_hid_lookup(char c, ascii_hid_key_t *out);

#ifdef __cplusplus
}
#endif
