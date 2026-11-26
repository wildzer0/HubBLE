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
    bool confident; /**< Not a palm (the device's Confidence usage; true if it declares none). */
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
    bool valid;                           /**< A touch is in progress; prev[] holds its last positions. */
    uint8_t nprev;                        /**< Entries in prev[]. */
    hid_contact_t prev[HID_MAX_CONTACTS]; /**< Positions in the previous report of this touch. */
    uint32_t last_ms;                     /**< Time of the previous report of this touch. */
} hid_abs_state_t;

/** A pause in absolute reports longer than this means the finger was lifted: the next one starts a new touch. */
#define HID_ABS_TOUCH_GAP_MS 60u

/**
 * @brief Turn absolute X/Y positions into relative movement, in place (the mouse fallback for touchpads).
 *
 * Touchpads that report absolute positions would otherwise pin the pointer to a screen corner. No-op unless the
 * event has absolute X/Y and no relative X/Y: some touchpads send both, and then the relative axes carry the movement.
 * Movement is that of the finger that moved most since the previous report; a lone finger stays the same finger even
 * if its contact ID changes. The first report of a touch moves nothing. A touchpad report with no finger down ends the
 * touch; so does a gap of HID_ABS_TOUCH_GAP_MS. Gestures (tap, scroll) are left to hosts that drive the touchpad
 * natively (see usb_hid_touchpad_active()).
 *
 * @param st     Per-device state.
 * @param ev     Event from hid_norm_decode().
 * @param now_ms Current time in milliseconds.
 */
void hid_norm_abs_to_rel(hid_abs_state_t *st, hid_norm_event_t *ev, uint32_t now_ms);

/**
 * @brief Range and physical size of a device's touchpad, from its report map, corrected for pads known to misreport.
 *
 * Uses the first absolute X and Y input fields of a report that also carries a Tip Switch. Without a usable physical
 * size the size is derived from an assumed resolution of HID_TP_ASSUMED_UNITS_PER_MM.
 *
 * @param layout Parsed layout.
 * @param[out] g Geometry (logical ranges start at 0; see x_min / y_min).
 * @param[out] x_min Logical minimum of X (positions are sent as x - x_min).
 * @param[out] y_min Logical minimum of Y.
 * @return false if the layout has no touchpad fingers.
 */
bool hid_norm_touchpad_geometry(const hid_layout_t *layout, hid_tp_geometry_t *g, int32_t *x_min, int32_t *y_min);

/** Resolution assumed for touchpads whose report map gives no physical size. */
#define HID_TP_ASSUMED_UNITS_PER_MM 30

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
