/**
 * @file hid_normalizer.c
 * @brief Report decoding to normalized events (see hid_normalizer.h).
 *
 * Dependencies: hid_parser, hid_report. Threading: stateless.
 */
#include "hid_normalizer.h"

#include <string.h>

/** Generic Desktop usages the mouse decoder needs. */
#define USAGE_X             0x30
#define USAGE_Y             0x31
#define USAGE_WHEEL         0x38
#define USAGE_SYSTEM_SLEEP  0x82
#define USAGE_SYSTEM_WAKEUP 0x83
/** Consumer usage "AC Pan" (horizontal scroll). */
#define USAGE_AC_PAN 0x238
/** First/last keyboard-page modifier usage. */
#define USAGE_MOD_FIRST 0xE0
#define USAGE_MOD_LAST  0xE7
/** LED page usages. */
#define USAGE_LED_NUM_LOCK    1
#define USAGE_LED_CAPS_LOCK   2
#define USAGE_LED_SCROLL_LOCK 3

/**
 * @brief Usage selected by the value of an Array item element (HID 1.11 §6.2.2.5: the value indexes the usages).
 *
 * @param f Field (array).
 * @param v Element value.
 * @return Usage ID, or 0 when the value selects nothing.
 */
static uint16_t array_usage(const hid_field_t *f, int32_t v) {
    int64_t idx = (int64_t)v - f->logical_min;
    if (idx < 0) {
        return 0;
    }
    if (f->usage_count > 0) {
        return idx < f->usage_count ? f->usages[idx] : 0;
    }
    int64_t u = (int64_t)f->usage_min + idx;
    return (u <= f->usage_max && u <= 0xFFFF) ? (uint16_t)u : 0;
}

/**
 * @brief Add a pressed key usage to the event (de-duplicated, extra keys beyond 6 are dropped).
 *
 * @param ev    Event.
 * @param usage Key usage (page 0x07).
 */
static void add_key(hid_norm_event_t *ev, uint16_t usage) {
    /* 0 = no event; 1..3 are the keyboard error/rollover codes (HID Usage Tables 0x07). */
    if (usage < 4 || usage > 0xFF) {
        return;
    }
    if (usage >= USAGE_MOD_FIRST && usage <= USAGE_MOD_LAST) {
        ev->modifiers |= (uint8_t)(1u << (usage - USAGE_MOD_FIRST));
        return;
    }
    for (unsigned i = 0; i < HID_KBD_MAX_KEYS; i++) {
        if (ev->keys[i] == usage) {
            return;
        }
        if (ev->keys[i] == 0) {
            ev->keys[i] = (uint8_t)usage;
            return;
        }
    }
}

/**
 * @brief Decode a keyboard-page field.
 *
 * Variable fields: each element is one key/modifier, pressed when non-zero. Array fields: each element's
 * value selects a usage (array_usage()); 0 means "no key" and 1..3 are error codes, both ignored.
 *
 * @param f    Field.
 * @param data Payload.
 * @param len  Payload length.
 * @param ev   Event to update.
 */
static void decode_keyboard(const hid_field_t *f, const uint8_t *data, size_t len, hid_norm_event_t *ev) {
    ev->has_keyboard = true;
    for (unsigned i = 0; i < f->count; i++) {
        int32_t v;
        if (!hid_field_value(f, i, data, len, &v)) {
            break;
        }
        if (f->flags & HID_FLAG_VARIABLE) {
            if (v != 0) {
                add_key(ev, hid_field_usage(f, i));
            }
        } else {
            add_key(ev, array_usage(f, v));
        }
    }
}

/**
 * @brief Decode a consumer-page field: report the first active usage (the USB report carries one).
 *
 * @param f    Field.
 * @param data Payload.
 * @param len  Payload length.
 * @param ev   Event to update.
 */
static void decode_consumer(const hid_field_t *f, const uint8_t *data, size_t len, hid_norm_event_t *ev) {
    for (unsigned i = 0; i < f->count; i++) {
        int32_t v;
        if (!hid_field_value(f, i, data, len, &v)) {
            break;
        }
        uint16_t usage = 0;
        if (f->flags & HID_FLAG_VARIABLE) {
            if (v != 0) {
                usage = hid_field_usage(f, i);
            }
        } else {
            usage = array_usage(f, v);
        }
        if (usage == USAGE_AC_PAN && (f->flags & HID_FLAG_RELATIVE)) {
            continue; /* Pan belongs to the mouse decoder; not a key. */
        }
        ev->has_consumer = true;
        if (usage != 0 && ev->consumer == 0 && usage <= HID_CONSUMER_USAGE_MAX) {
            ev->consumer = usage;
        }
    }
}

/**
 * @brief Decode Generic Desktop fields: pointer axes, wheel and system control.
 *
 * @param f    Field.
 * @param data Payload.
 * @param len  Payload length.
 * @param ev   Event to update.
 */
static void decode_desktop(const hid_field_t *f, const uint8_t *data, size_t len, hid_norm_event_t *ev) {
    for (unsigned i = 0; i < f->count; i++) {
        int32_t v;
        if (!hid_field_value(f, i, data, len, &v)) {
            break;
        }
        uint16_t usage = hid_field_usage(f, i);
        switch (usage) {
        case USAGE_X:
            ev->has_mouse = true;
            ev->dx += v;
            break;
        case USAGE_Y:
            ev->has_mouse = true;
            ev->dy += v;
            break;
        case USAGE_WHEEL:
            ev->has_mouse = true;
            ev->wheel += v;
            break;
        case USAGE_SYSTEM_SLEEP:
            ev->has_system = true;
            if (v != 0) {
                ev->system |= HID_SYS_SLEEP;
            }
            break;
        case USAGE_SYSTEM_WAKEUP:
            ev->has_system = true;
            if (v != 0) {
                ev->system |= HID_SYS_WAKEUP;
            }
            break;
        default:
            break;
        }
    }
}

/** @copydoc hid_norm_decode */
bool hid_norm_decode(const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len,
                     hid_norm_event_t *ev) {
    memset(ev, 0, sizeof(*ev));
    if (layout == NULL || data == NULL) {
        return false;
    }
    bool any = false;
    for (unsigned n = 0; n < layout->nfields; n++) {
        const hid_field_t *f = &layout->fields[n];
        if (f->dir != HID_REPORT_IN || f->report_id != report_id) {
            continue;
        }
        switch (f->usage_page) {
        case HID_PAGE_KEYBOARD:
            decode_keyboard(f, data, len, ev);
            any = true;
            break;
        case HID_PAGE_CONSUMER: {
            /* AC Pan is a relative consumer usage used as horizontal scroll by mice. */
            if ((f->flags & HID_FLAG_RELATIVE) && f->usage_count > 0) {
                for (unsigned i = 0; i < f->count; i++) {
                    int32_t v;
                    if (hid_field_usage(f, i) == USAGE_AC_PAN && hid_field_value(f, i, data, len, &v)) {
                        ev->has_mouse = true;
                        ev->pan += v;
                        any = true;
                    }
                }
            } else {
                decode_consumer(f, data, len, ev);
                any = true;
            }
            break;
        }
        case HID_PAGE_BUTTON:
            ev->has_mouse = true;
            for (unsigned i = 0; i < f->count && i < 5; i++) {
                int32_t v;
                if (hid_field_value(f, i, data, len, &v) && v != 0) {
                    ev->buttons |= (uint8_t)(1u << i);
                }
            }
            any = true;
            break;
        case HID_PAGE_GENERIC_DESKTOP:
            decode_desktop(f, data, len, ev);
            any = true;
            break;
        default:
            break; /* Vendor pages and anything unknown are ignored. */
        }
    }
    return any;
}

/** @copydoc hid_norm_build_led_report */
size_t hid_norm_build_led_report(const hid_layout_t *layout, uint8_t leds, uint8_t *id, uint8_t *out, size_t cap) {
    *id = 0;
    if (layout == NULL) {
        return 0;
    }
    size_t total = 0;
    for (unsigned n = 0; n < layout->nfields; n++) {
        const hid_field_t *f = &layout->fields[n];
        if (f->dir != HID_REPORT_OUT || f->usage_page != HID_PAGE_LED || (f->flags & HID_FLAG_VARIABLE) == 0) {
            continue;
        }
        if (total == 0) {
            *id = f->report_id;
            total = hid_layout_report_bytes(layout, HID_REPORT_OUT, f->report_id);
            if (total == 0 || total > cap) {
                return 0;
            }
            memset(out, 0, total);
        } else if (f->report_id != *id) {
            continue; /* Only fill one output report (the keyboard one). */
        }
        for (unsigned i = 0; i < f->count; i++) {
            uint16_t usage = hid_field_usage(f, i);
            bool on = (usage == USAGE_LED_NUM_LOCK && (leds & HID_LED_NUM_LOCK)) ||
                      (usage == USAGE_LED_CAPS_LOCK && (leds & HID_LED_CAPS_LOCK)) ||
                      (usage == USAGE_LED_SCROLL_LOCK && (leds & HID_LED_SCROLL_LOCK));
            uint32_t pos = (uint32_t)f->bit_offset + i * f->bit_size;
            if (on && pos < total * 8u) {
                out[pos >> 3] |= (uint8_t)(1u << (pos & 7));
            }
        }
    }
    return total;
}

/** @copydoc hid_norm_boot_keyboard_layout */
void hid_norm_boot_keyboard_layout(hid_layout_t *layout) {
    (void)hid_parser_parse(hid_kbd_report_desc, HID_KBD_REPORT_DESC_LEN, layout);
}
