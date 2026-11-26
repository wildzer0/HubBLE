/**
 * @file hid_normalizer.h
 * @brief Decode raw HID input reports into the dongle's normalized events (pure C).
 *
 * Responsibilities: using a hid_layout_t from hid_parser, translate a report received from a BLE peripheral
 * into: keyboard state (modifiers + up to 6 keys), one consumer usage, system-control bits, and mouse
 * buttons/deltas/wheel/pan (CLAUDE.md §3.3). Also builds the keyboard LED output report. Unknown usages are
 * ignored; short or malformed reports never read out of bounds.
 * Dependencies: hid_parser, hid_report (constants only). Threading: stateless pure functions.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_parser.h"
#include "hid_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What one decoded report says. A "has_*" flag tells which parts the report carried. */
/** Most touchpad fingers tracked at once. */
#define HID_MAX_CONTACTS 5

/** One absolute position: a touchpad finger, or the single position of an absolute pointer. */
typedef struct {
    int32_t id; /**< Contact ID (0 if the device declares none). */
    int32_t x;  /**< Absolute X. */
    int32_t y;  /**< Absolute Y. */
} hid_contact_t;

typedef struct {
    bool has_keyboard;              /**< The report carried keyboard fields. */
    uint8_t modifiers;              /**< Modifier bits (bit n = usage 0xE0+n). */
    uint8_t keys[HID_KBD_MAX_KEYS]; /**< Pressed key usages (page 0x07), 0 = unused. */
    bool has_consumer;              /**< The report carried consumer fields. */
    uint16_t consumer;              /**< Pressed consumer usage, 0 if none. */
    bool has_system;                /**< The report carried system-control fields. */
    uint8_t system;                 /**< hid_system_bits_t. */
    bool has_mouse;                 /**< The report carried mouse fields (buttons or axes). */
    uint8_t buttons;                /**< hid_mouse_button_t bits. */
    int32_t dx;                     /**< Relative X. */
    int32_t dy;                     /**< Relative Y. */
    int32_t wheel;                  /**< Vertical wheel. */
    int32_t pan;                    /**< Horizontal wheel (AC Pan). */
    bool rel_xy;                    /**< The report carried relative X/Y (summed into dx/dy). */
    bool abs_xy;                    /**< The report carried absolute X/Y, see hid_norm_abs_to_rel(). */
    uint8_t ncontacts;              /**< Fingers down (or 1 for an absolute pointer without fingers). */
    hid_contact_t contacts[HID_MAX_CONTACTS]; /**< Their positions (if abs_xy). */
    bool touch_report;              /**< The report carried touchpad fingers (Tip Switch), touching or not. */
} hid_norm_event_t;

/** Per-device state for hid_norm_abs_to_rel(). Zero-initialize. */
typedef struct {
    bool valid;                         /**< A touch is in progress; prev[] holds its last positions. */
    uint8_t nprev;                      /**< Entries in prev[]. */
    hid_contact_t prev[HID_MAX_CONTACTS]; /**< Positions in the previous report of this touch. */
    uint32_t last_ms;                   /**< Time of the previous report of this touch. */
    uint32_t down_ms;                   /**< Time the current touch started. */
    int32_t travel;                     /**< Distance moved so far while held back (sum of |dx| + |dy|). */
    int32_t held_dx;                    /**< Movement held back while the touch may still be a tap. */
    int32_t held_dy;                    /**< See held_dx. */
    bool moving;                        /**< The touch can no longer be a tap: movement goes out directly. */
    bool pressed;                       /**< A button was held during the current touch (a click, not a tap). */
    bool multi;                         /**< More than one finger was down during the current touch. */
    bool drag;                          /**< The touch started right after a tap: it holds the left button. */
    bool tap_recent;                    /**< The previous touch was a tap ending at tap_end_ms. */
    uint32_t tap_end_ms;                /**< When the previous tap ended. */
    uint32_t last_move_ms;              /**< Last report that moved more than 1 unit (rest detection). */
    bool lift_hold;                     /**< Movement after a rest is held back: it may be lift roll. */
    uint32_t lift_hold_ms;              /**< When lift_hold started. */
    int32_t rem_x;                      /**< Remainder of the touchpad speed scaling, in 1/HID_TOUCHPAD_SPEED_DEN. */
    int32_t rem_y;                      /**< See rem_x. */
} hid_abs_state_t;

/** A pause in absolute reports longer than this means the finger was lifted: the next one starts a new touch. */
#define HID_ABS_TOUCH_GAP_MS 60u
/** A one-finger touch shorter than this that ends by a lift is a tap (a left click). */
#define HID_TAP_MAX_MS 200u
/** A touch that moved further than this (in device units) is a move, not a tap. */
#define HID_TAP_MAX_TRAVEL 100
/** A touch that starts within this time after a tap drags: the left button is held until the finger lifts. */
#define HID_TAP_DRAG_MS 300u
/** A finger that moved at most 1 unit per report for this long is resting. */
#define HID_REST_MS 60u
/** Movement of a resting finger is held back this long: if the finger lifts meanwhile, it was lift roll. */
#define HID_LIFT_HOLD_MS 30u
/** Held-back movement after a rest larger than this is a real move and goes out at once. */
#define HID_LIFT_MAX_TRAVEL 40
/** Touchpad speed as a fraction: finger movement in device units is scaled by NUM/DEN before it goes out. */
#define HID_TOUCHPAD_SPEED_NUM 1
#define HID_TOUCHPAD_SPEED_DEN 2

/**
 * @brief Turn absolute X/Y positions into relative movement, with tap-to-click and tap-and-drag, in place.
 *
 * Touchpads that report absolute positions would otherwise pin the pointer to a screen corner. No-op unless the
 * event has absolute X/Y and no relative X/Y: some touchpads send both, and then the relative axes carry the movement.
 *
 * - Movement is that of the finger that moved most since the previous report (so a clickpad can be held with one
 *   finger and dragged with another). A single finger stays the same finger even if its contact ID changes.
 * - While a touch may still be a tap (under HID_TAP_MAX_MS and HID_TAP_MAX_TRAVEL), movement is held back, so taps
 *   never move the pointer; once it is a move, the held-back movement goes out at once.
 * - A touch that starts within HID_TAP_DRAG_MS after a tap holds the left button in ev->buttons once it moves or
 *   lasts (tap-and-drag); a quick second tap stays a tap (double click).
 * - While a button of the device is held, a single finger does not move the pointer (pressing a clickpad flattens
 *   the finger, which would move what is being clicked); a second finger does (hold with the thumb, drag with another).
 * - When a resting finger starts to move, the movement is held back for HID_LIFT_HOLD_MS (or until it exceeds
 *   HID_LIFT_MAX_TRAVEL) and dropped if the finger lifts meanwhile: fingers roll a little as they lift.
 * - Touchpad movement is scaled by HID_TOUCHPAD_SPEED_NUM/HID_TOUCHPAD_SPEED_DEN, keeping the remainder.
 * - A touchpad report with no finger down ends the touch; so does a gap of HID_ABS_TOUCH_GAP_MS.
 *
 * @param st     Per-device state.
 * @param ev     Event from hid_norm_decode().
 * @param now_ms Current time in milliseconds.
 * @return true if this report ended a tap: a short one-finger touch that barely moved and clicked no button. The
 *         touchpad leaves tap-to-click to the host's driver, which a plain USB mouse does not have, so the caller
 *         clicks.
 */
bool hid_norm_abs_to_rel(hid_abs_state_t *st, hid_norm_event_t *ev, uint32_t now_ms);

/**
 * @brief Decode one input report.
 *
 * @param layout    Parsed layout of the device.
 * @param report_id Report ID of the notification (0 if the device uses none; for HOGP it comes from the
 *                  characteristic's Report Reference descriptor).
 * @param data      Report payload without the report ID byte.
 * @param len       Payload length.
 * @param[out] ev   Receives the decoded event (always cleared first).
 * @return true if at least one known field was decoded from this report.
 */
bool hid_norm_decode(const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len,
                     hid_norm_event_t *ev);

/**
 * @brief Build the keyboard LED output report for a device.
 *
 * @param layout     Parsed layout of the device.
 * @param leds       hid_led_bits_t bit field wanted on.
 * @param[out] id    Report ID to write to (0 if none).
 * @param[out] out   Payload buffer.
 * @param cap        Buffer capacity in bytes.
 * @return Payload length in bytes, or 0 if the device has no LED output field (or it does not fit @p cap).
 */
size_t hid_norm_build_led_report(const hid_layout_t *layout, uint8_t leds, uint8_t *id, uint8_t *out, size_t cap);

/**
 * @brief Fill a layout with the standard boot keyboard (HID 1.11 App. B.1), for devices without a Report Map.
 *
 * @param[out] layout Receives the layout.
 */
void hid_norm_boot_keyboard_layout(hid_layout_t *layout);

/**
 * @brief Does the layout have relative or absolute pointer axes (Generic Desktop X/Y input)?
 *
 * Pairing uses this to pick the quarantine challenge: many trackballs and mice also declare a keyboard collection
 * (shortcut keys), which must not make them look like keyboards.
 *
 * @param layout Parsed layout.
 * @return true if an input field carries X or Y.
 */
bool hid_norm_is_pointer(const hid_layout_t *layout);

/**
 * @brief Is this layout a real keyboard (typed keys), as opposed to a mouse with shortcut keys?
 *
 * @param layout Parsed layout.
 * @return true for a keyboard.
 */
bool hid_norm_is_keyboard(const hid_layout_t *layout);

/**
 * @brief Which pairing challenge fits a device: the code to type (true) or the click sequence (false)?
 *
 * The advertised GAP appearance decides when it names a keyboard or a pointing device; otherwise the report map does.
 *
 * @param appearance GAP appearance seen while scanning, or 0 if the device did not advertise one.
 * @param layout     The device's parsed layout.
 * @return true if the user can type on the device.
 */
bool hid_norm_pairing_is_keyboard(uint16_t appearance, const hid_layout_t *layout);

#ifdef __cplusplus
}
#endif
