/**
 * @file hid_report.h
 * @brief USB HID report descriptors, report layouts and the multi-source merge logic (pure C).
 *
 * Responsibilities:
 *  - define the two USB HID interfaces the dongle exposes (CLAUDE.md §3.2):
 *      * interface 0: boot-protocol keyboard (no report ID, 8-byte input, 1-byte LED output), so that it
 *        works in BIOS/UEFI which only understand the boot keyboard layout;
 *      * interface 1: mouse (report ID 1), consumer control (ID 2) and system control (ID 3).
 *  - merge the input of several sources (up to 4 BLE peripherals plus one debug/injection source) into the
 *    single state the host sees: keyboard modifiers/keys are united, mouse buttons are united and mouse
 *    deltas are summed until they are sent.
 *
 * Dependencies: libc only (host-testable, CLAUDE.md §3.1). No ESP-IDF API is used here.
 * Threading: hid_merge_t has NO internal locking; the owner (usb_hid) serialises access.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------------ */
/* Descriptors                                                                                       */
/* ------------------------------------------------------------------------------------------------ */

/** HID report IDs used on interface 1. Interface 0 (boot keyboard) has no report ID by design. */
typedef enum {
    HID_RID_MOUSE = 1,    /**< Mouse report. */
    HID_RID_CONSUMER = 2, /**< Consumer control (media keys) report. */
    HID_RID_SYSTEM = 3,   /**< System control (sleep / wake) report. */
} hid_report_id_t;

/** Number of simultaneously reported keys in the boot keyboard report (6KRO). */
#define HID_KBD_MAX_KEYS 6
/** Boot-keyboard "phantom state" usage reported when more than HID_KBD_MAX_KEYS keys are down (HID 1.11 App. B.1). */
#define HID_KBD_ROLLOVER_ERROR 0x01
/** Highest consumer usage the descriptor declares (logical maximum). */
#define HID_CONSUMER_USAGE_MAX 0x03FF

/** Keyboard LED output-report bits (HID Usage Page 0x08, boot keyboard). */
typedef enum {
    HID_LED_NUM_LOCK = 1u << 0,    /**< Num Lock. */
    HID_LED_CAPS_LOCK = 1u << 1,   /**< Caps Lock. */
    HID_LED_SCROLL_LOCK = 1u << 2, /**< Scroll Lock. */
} hid_led_bits_t;

/** System-control bits (Generic Desktop usages 0x82 / 0x83). */
typedef enum {
    HID_SYS_SLEEP = 1u << 0,  /**< System Sleep. */
    HID_SYS_WAKEUP = 1u << 1, /**< System Wake Up. */
} hid_system_bits_t;

/** Mouse button bits (Button usage page, buttons 1..5). */
typedef enum {
    HID_MOUSE_LEFT = 1u << 0,    /**< Primary button. */
    HID_MOUSE_RIGHT = 1u << 1,   /**< Secondary button. */
    HID_MOUSE_MIDDLE = 1u << 2,  /**< Wheel button. */
    HID_MOUSE_BACK = 1u << 3,    /**< Button 4. */
    HID_MOUSE_FORWARD = 1u << 4, /**< Button 5. */
} hid_mouse_button_t;

/** Mask of the valid mouse button bits. */
#define HID_MOUSE_BUTTON_MASK 0x1Fu

/** Boot keyboard input report (interface 0, no report ID), 8 bytes on the wire. */
typedef struct __attribute__((packed)) {
    uint8_t modifiers;              /**< Bit field: LCtrl, LShift, LAlt, LGui, RCtrl, RShift, RAlt, RGui. */
    uint8_t reserved;               /**< Always 0 (boot protocol). */
    uint8_t keys[HID_KBD_MAX_KEYS]; /**< Key usages (page 0x07), 0 = empty slot. */
} hid_kbd_report_t;

/** Mouse input report (interface 1, report ID 1; the ID byte is added by the USB stack), 7 bytes. */
typedef struct __attribute__((packed)) {
    uint8_t buttons; /**< hid_mouse_button_t bit field. */
    int16_t x;       /**< Relative X movement, little endian. */
    int16_t y;       /**< Relative Y movement, little endian. */
    int8_t wheel;    /**< Vertical wheel (positive = away from the user). */
    int8_t pan;      /**< Horizontal wheel / AC Pan (positive = right). */
} hid_mouse_in_t;

/** Size in bytes of the boot keyboard report. */
#define HID_KBD_REPORT_SIZE 8
/** Size in bytes of the mouse report (without report ID). */
#define HID_MOUSE_REPORT_SIZE 7

/** Boot keyboard report descriptor (interface 0). */
extern const uint8_t hid_kbd_report_desc[];
/** Length in bytes of hid_kbd_report_desc (a constant so the USB configuration descriptor can use it). */
#define HID_KBD_REPORT_DESC_LEN 63
/** Mouse + consumer + system report descriptor (interface 1). */
extern const uint8_t hid_ctrl_report_desc[];
/** Length in bytes of hid_ctrl_report_desc. */
#define HID_CTRL_REPORT_DESC_LEN 129

/* ------------------------------------------------------------------------------------------------ */
/* Merge logic                                                                                       */
/* ------------------------------------------------------------------------------------------------ */

/** Number of merge sources: 4 BLE peripherals (CLAUDE.md §3.3) plus one debug/injection source. */
#define HID_MERGE_SOURCES 5
/** Source index reserved for the debug "inject test input" feature. */
#define HID_SRC_DEBUG 4

/** Mouse axes, used to index the per-source movement accumulators. */
typedef enum {
    HID_AXIS_X = 0, /**< Relative X. */
    HID_AXIS_Y,     /**< Relative Y. */
    HID_AXIS_WHEEL, /**< Vertical wheel. */
    HID_AXIS_PAN,   /**< Horizontal wheel. */
    HID_AXIS_COUNT, /**< Number of axes. */
} hid_axis_t;

/** Per-source input state held by hid_merge_t. */
typedef struct {
    uint8_t modifiers;              /**< Modifier bits currently held by this source. */
    uint8_t keys[HID_KBD_MAX_KEYS]; /**< Keys held by this source (0 = unused slot). */
    uint8_t mouse_buttons;          /**< Mouse buttons held by this source. */
    uint16_t consumer;              /**< Consumer usage held by this source (0 = none). */
    uint8_t system;                 /**< hid_system_bits_t held by this source. */
    int32_t axis[HID_AXIS_COUNT];   /**< Movement accumulated since the last hid_merge_take_mouse(), per hid_axis_t. */
} hid_source_state_t;

/** Merge state: one hid_source_state_t per source. */
typedef struct {
    hid_source_state_t src[HID_MERGE_SOURCES]; /**< Per-source state. */
} hid_merge_t;

/**
 * @brief Reset a merge context (all sources idle).
 *
 * @param m Context to initialise.
 */
void hid_merge_init(hid_merge_t *m);

/**
 * @brief Release everything held by one source (call when its peripheral disconnects, so no key stays stuck).
 *
 * @param m   Context.
 * @param src Source index (< HID_MERGE_SOURCES); out-of-range values are ignored.
 */
void hid_merge_clear_source(hid_merge_t *m, unsigned src);

/**
 * @brief Replace the keyboard state of one source.
 *
 * @param m         Context.
 * @param src       Source index.
 * @param modifiers Modifier bit field.
 * @param keys      Pressed key usages (page 0x07); zero entries and modifier usages 0xE0..0xE7 are ignored.
 * @param nkeys     Number of entries in @p keys. Entries beyond HID_KBD_MAX_KEYS are still stored as far as
 *                  they fit; the merge reports rollover if the union exceeds 6 keys.
 */
void hid_merge_set_keyboard(hid_merge_t *m, unsigned src, uint8_t modifiers, const uint8_t *keys, size_t nkeys);

/**
 * @brief Replace the consumer-control usage of one source (0 releases it).
 *
 * @param m     Context.
 * @param src   Source index.
 * @param usage Consumer usage; values above HID_CONSUMER_USAGE_MAX are treated as 0 (unknown usage ignored).
 */
void hid_merge_set_consumer(hid_merge_t *m, unsigned src, uint16_t usage);

/**
 * @brief Replace the system-control bits of one source.
 *
 * @param m    Context.
 * @param src  Source index.
 * @param bits hid_system_bits_t bit field (other bits are dropped).
 */
void hid_merge_set_system(hid_merge_t *m, unsigned src, uint8_t bits);

/**
 * @brief Update the mouse of one source: replace its buttons and add movement to the accumulators.
 *
 * @param m       Context.
 * @param src     Source index.
 * @param buttons hid_mouse_button_t bit field (replaces the previous state).
 * @param dx      Relative X to add.
 * @param dy      Relative Y to add.
 * @param wheel   Vertical wheel to add.
 * @param pan     Horizontal wheel to add.
 */
void hid_merge_mouse(hid_merge_t *m, unsigned src, uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel, int32_t pan);

/**
 * @brief Build the merged keyboard report: union of modifiers, union of distinct keys (in source order).
 *
 * If more than HID_KBD_MAX_KEYS distinct keys are down, every key slot is HID_KBD_ROLLOVER_ERROR.
 *
 * @param m   Context.
 * @param out Receives the report.
 */
void hid_merge_get_keyboard(const hid_merge_t *m, hid_kbd_report_t *out);

/**
 * @brief Merged consumer usage: the first source (lowest index) holding a non-zero usage wins, since the
 *        report can carry a single usage.
 *
 * @param m Context.
 * @return Usage, or 0 if none is held.
 */
uint16_t hid_merge_get_consumer(const hid_merge_t *m);

/**
 * @brief Merged system-control bits (union).
 *
 * @param m Context.
 * @return hid_system_bits_t bit field.
 */
uint8_t hid_merge_get_system(const hid_merge_t *m);

/**
 * @brief Build the merged mouse report and consume the accumulated movement.
 *
 * Buttons are the union over the sources. Each axis is the sum of the sources, clamped to the report range
 * (int16 for X/Y, int8 for wheel/pan). Movement that does not fit is kept in the accumulators and sent with
 * the next report, so nothing is lost on a fast flick.
 *
 * @param m   Context.
 * @param out Receives the report.
 * @return true if the report differs from an idle one (has movement), false if only buttons (or nothing)
 *         are set; callers use hid_mouse_pending() to decide whether to send on button changes.
 */
bool hid_merge_take_mouse(hid_merge_t *m, hid_mouse_in_t *out);

/**
 * @brief Whether movement is waiting to be sent.
 *
 * @param m Context.
 * @return true if any accumulator is non-zero.
 */
bool hid_merge_mouse_pending(const hid_merge_t *m);

#ifdef __cplusplus
}
#endif
