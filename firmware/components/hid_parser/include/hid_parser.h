/**
 * @file hid_parser.h
 * @brief Generic HID report-descriptor parser (pure C, no allocation, fuzz-friendly).
 *
 * Responsibilities: turn an arbitrary HID report map (as read from a peripheral's HOGP Report Map
 * characteristic, CLAUDE.md §3.3) into a flat list of Input/Output fields (report ID, bit position, usage
 * page/usages, logical range, flags) that hid_normalizer can use to decode reports. Descriptors come from
 * untrusted peripherals: every access is bounds-checked, capacities are fixed, unknown items are skipped and
 * malformed input returns an error code. It never crashes and never allocates.
 *
 * Supported: global items (Usage Page, Logical Min/Max, Report Size/Count/ID, Push/Pop), local items (Usage,
 * Usage Min/Max, extended 32-bit usages), main items (Input, Output, Collection, End Collection), long items
 * (skipped). Feature items are laid out but not stored. Constant (padding) items are not stored either.
 * Dependencies: libc only. Threading: pure functions, no shared state.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of stored (non-constant) Input/Output fields per device. */
#define HID_PARSER_MAX_FIELDS 40
/** Maximum number of explicit usages kept per field (items listing usages one by one). */
#define HID_PARSER_MAX_USAGES 12
/** Maximum distinct report IDs tracked per direction. */
#define HID_PARSER_MAX_REPORT_IDS 8
/** Maximum collection nesting depth accepted. */
#define HID_PARSER_MAX_DEPTH 12
/** Maximum Push/Pop depth. */
#define HID_PARSER_MAX_PUSH 4
/** Largest report payload considered valid, in bits (a BLE notification carries at most 512 bytes). */
#define HID_PARSER_MAX_REPORT_BITS (512 * 8)

/** HID usage pages the dongle understands. */
enum {
    HID_PAGE_GENERIC_DESKTOP = 0x01, /**< X/Y/Wheel, System Control. */
    HID_PAGE_KEYBOARD = 0x07,        /**< Keys and modifiers (0xE0..0xE7). */
    HID_PAGE_LED = 0x08,             /**< Keyboard LEDs. */
    HID_PAGE_BUTTON = 0x09,          /**< Mouse buttons. */
    HID_PAGE_CONSUMER = 0x0C,        /**< Media keys, AC Pan. */
};

/** Direction of a report. */
typedef enum {
    HID_REPORT_IN = 0,  /**< Input report: peripheral -> dongle. */
    HID_REPORT_OUT = 1, /**< Output report: dongle -> peripheral (keyboard LEDs). */
} hid_dir_t;

/** Bits of hid_field_t::flags (the data bits of the HID main item). */
enum {
    HID_FLAG_VARIABLE = 1u << 0, /**< Variable (each element is a separate value); else Array. */
    HID_FLAG_RELATIVE = 1u << 1, /**< Relative (deltas); else Absolute. */
};

/** Result of hid_parser_parse(). */
typedef enum {
    HID_PARSE_OK = 0,    /**< Parsed; the layout is complete. */
    HID_PARSE_TRUNCATED, /**< An item ran past the end of the descriptor; fields parsed so far are kept. */
    HID_PARSE_TOO_MANY,  /**< More fields/reports than the capacity; fields parsed so far are kept. */
    HID_PARSE_BAD_DEPTH, /**< Collection or Push nesting beyond the limit or unbalanced End Collection. */
    HID_PARSE_INVALID,   /**< Invalid argument (NULL, empty descriptor). */
} hid_parse_result_t;

/** One Input or Output field (an Input/Output main item that is not constant padding). */
typedef struct {
    uint8_t report_id;                      /**< Report ID (0 when the device uses no report IDs). */
    uint8_t dir;                            /**< hid_dir_t. */
    uint8_t flags;                          /**< HID_FLAG_* bits. */
    uint8_t bit_size;                       /**< Size of one element in bits (1..32). */
    uint16_t count;                         /**< Number of elements. */
    uint16_t bit_offset;                    /**< Bit position of the first element inside the report payload. */
    uint16_t usage_page;                    /**< Usage page of the usages below. */
    uint8_t usage_count;                    /**< Explicit usages stored in @c usages (0 when a range is used). */
    uint16_t usages[HID_PARSER_MAX_USAGES]; /**< Explicit usage IDs; element i uses usages[min(i, usage_count-1)]. */
    uint16_t usage_min;                     /**< Range start (valid when usage_count == 0): element i is usage_min+i. */
    uint16_t usage_max;                     /**< Range end. */
    int32_t logical_min;                    /**< Logical minimum (sign-extended). */
    int32_t logical_max;                    /**< Logical maximum. */
} hid_field_t;

/** Everything the normalizer needs to decode a device's reports. */
typedef struct {
    hid_field_t fields[HID_PARSER_MAX_FIELDS]; /**< Stored fields in descriptor order. */
    uint8_t nfields;                           /**< Number of valid entries in @c fields. */
    bool uses_report_ids;                      /**< At least one Report ID item was seen. */
    uint16_t primary_page;  /**< Usage page of the FIRST top-level (Application) collection, 0 if none. */
    uint16_t primary_usage; /**< Its usage (Generic Desktop: 0x06 keyboard, 0x02 mouse): what the device mainly is. */
    uint8_t n_ids[2];       /**< Number of distinct report IDs per direction. */
    uint8_t ids[2][HID_PARSER_MAX_REPORT_IDS];   /**< The distinct report IDs per direction. */
    uint16_t bits[2][HID_PARSER_MAX_REPORT_IDS]; /**< Payload size in bits of each of those reports. */
} hid_layout_t;

/**
 * @brief Parse a report descriptor.
 *
 * @param desc   Descriptor bytes.
 * @param len    Length in bytes.
 * @param layout Receives the layout (always initialised, even on error, with what was parsed before the error).
 * @return HID_PARSE_OK or an error code; on an error the partial layout may still be usable.
 */
hid_parse_result_t hid_parser_parse(const uint8_t *desc, size_t len, hid_layout_t *layout);

/**
 * @brief Payload size in bytes of one report (rounded up), excluding the report ID byte.
 *
 * @param layout Parsed layout.
 * @param dir    Direction.
 * @param id     Report ID (0 if the device uses none).
 * @return Size in bytes, or 0 if no such report exists.
 */
size_t hid_layout_report_bytes(const hid_layout_t *layout, hid_dir_t dir, uint8_t id);

/**
 * @brief Usage ID of element @p index of a field.
 *
 * @param f     Field.
 * @param index Element index (< f->count).
 * @return Usage ID inside f->usage_page.
 */
uint16_t hid_field_usage(const hid_field_t *f, unsigned index);

/**
 * @brief Extract an element's raw value from a report payload.
 *
 * @param f       Field.
 * @param index   Element index (< f->count).
 * @param data    Report payload (without the report ID byte).
 * @param len     Payload length in bytes.
 * @param[out] v  Receives the value, sign-extended when the field's logical minimum is negative.
 * @return false if the element lies (partly) outside the payload.
 */
bool hid_field_value(const hid_field_t *f, unsigned index, const uint8_t *data, size_t len, int32_t *v);

#ifdef __cplusplus
}
#endif
