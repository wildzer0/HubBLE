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
} hid_norm_event_t;

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

#ifdef __cplusplus
}
#endif
