/**
 * @file hid_report.c
 * @brief Descriptors and merge logic for the dongle's USB HID reports (see hid_report.h).
 *
 * Responsibilities: constant report descriptors and the pure merge functions.
 * Dependencies: libc only. Threading: none (caller serialises access to a hid_merge_t).
 */
#include "hid_report.h"

#include <string.h>

/** First and last modifier usage on the keyboard page (LCtrl..RGui); carried in the modifier byte, not as keys. */
#define USAGE_MOD_FIRST 0xE0
#define USAGE_MOD_LAST  0xE7

/* clang-format off */

/*
 * Interface 0: boot keyboard. Layout is the one in HID 1.11 Appendix B.1 so BIOS/UEFI can parse it without
 * a report descriptor: 1 byte modifiers, 1 reserved byte, 6 key bytes in; 5 LED bits + 3 pad bits out.
 */
const uint8_t hid_kbd_report_desc[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad) */
    0x19, 0xE0,       /*   Usage Minimum (Left Control) */
    0x29, 0xE7,       /*   Usage Maximum (Right GUI) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute): modifier byte */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x01,       /*   Input (Constant): reserved byte */
    0x05, 0x08,       /*   Usage Page (LEDs) */
    0x19, 0x01,       /*   Usage Minimum (Num Lock) */
    0x29, 0x05,       /*   Usage Maximum (Kana) */
    0x95, 0x05,       /*   Report Count (5) */
    0x75, 0x01,       /*   Report Size (1) */
    0x91, 0x02,       /*   Output (Data, Variable, Absolute): LED bits */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x03,       /*   Report Size (3) */
    0x91, 0x01,       /*   Output (Constant): LED padding */
    0x95, 0x06,       /*   Report Count (6) */
    0x75, 0x08,       /*   Report Size (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x65,       /*   Logical Maximum (101) */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x29, 0x65,       /*   Usage Maximum (101) */
    0x81, 0x00,       /*   Input (Data, Array): 6 key slots */
    0xC0,             /* End Collection */
};
_Static_assert(sizeof(hid_kbd_report_desc) == HID_KBD_REPORT_DESC_LEN, "update HID_KBD_REPORT_DESC_LEN");

/*
 * Interface 1: mouse (ID 1), consumer control (ID 2), system control (ID 3).
 * A single mouse collection with 16-bit X/Y, 8-bit wheel and AC Pan (Consumer 0x0238), as in CLAUDE.md §3.2.
 */
const uint8_t hid_ctrl_report_desc[] = {
    /* --- Mouse, report ID 1: 7 bytes --- */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x02,       /* Usage (Mouse) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, HID_RID_MOUSE, /*   Report ID (1) */
    0x09, 0x01,       /*   Usage (Pointer) */
    0xA1, 0x00,       /*   Collection (Physical) */
    0x05, 0x09,       /*     Usage Page (Button) */
    0x19, 0x01,       /*     Usage Minimum (Button 1) */
    0x29, 0x05,       /*     Usage Maximum (Button 5) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x01,       /*     Logical Maximum (1) */
    0x95, 0x05,       /*     Report Count (5) */
    0x75, 0x01,       /*     Report Size (1) */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute): 5 buttons */
    0x95, 0x01,       /*     Report Count (1) */
    0x75, 0x03,       /*     Report Size (3) */
    0x81, 0x01,       /*     Input (Constant): button padding */
    0x05, 0x01,       /*     Usage Page (Generic Desktop) */
    0x09, 0x30,       /*     Usage (X) */
    0x09, 0x31,       /*     Usage (Y) */
    0x16, 0x01, 0x80, /*     Logical Minimum (-32767) */
    0x26, 0xFF, 0x7F, /*     Logical Maximum (32767) */
    0x75, 0x10,       /*     Report Size (16) */
    0x95, 0x02,       /*     Report Count (2) */
    0x81, 0x06,       /*     Input (Data, Variable, Relative): X, Y */
    0x09, 0x38,       /*     Usage (Wheel) */
    0x15, 0x81,       /*     Logical Minimum (-127) */
    0x25, 0x7F,       /*     Logical Maximum (127) */
    0x75, 0x08,       /*     Report Size (8) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x06,       /*     Input (Data, Variable, Relative): vertical wheel */
    0x05, 0x0C,       /*     Usage Page (Consumer) */
    0x0A, 0x38, 0x02, /*     Usage (AC Pan) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x06,       /*     Input (Data, Variable, Relative): horizontal pan */
    0xC0,             /*   End Collection (Physical) */
    0xC0,             /* End Collection (Mouse) */

    /* --- Consumer control, report ID 2: one 16-bit usage --- */
    0x05, 0x0C,       /* Usage Page (Consumer) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, HID_RID_CONSUMER, /*   Report ID (2) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x03, /*   Logical Maximum (0x3FF) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x2A, 0xFF, 0x03, /*   Usage Maximum (0x3FF) */
    0x75, 0x10,       /*   Report Size (16) */
    0x95, 0x01,       /*   Report Count (1) */
    0x81, 0x00,       /*   Input (Data, Array) */
    0xC0,             /* End Collection */

    /* --- System control, report ID 3: Sleep and Wake Up bits (no Power Down on purpose) --- */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x80,       /* Usage (System Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, HID_RID_SYSTEM, /*   Report ID (3) */
    0x19, 0x82,       /*   Usage Minimum (System Sleep) */
    0x29, 0x83,       /*   Usage Maximum (System Wake Up) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x02,       /*   Report Count (2) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x06,       /*   Report Size (6) */
    0x81, 0x01,       /*   Input (Constant): padding */
    0xC0,             /* End Collection */
};
_Static_assert(sizeof(hid_ctrl_report_desc) == HID_CTRL_REPORT_DESC_LEN, "update HID_CTRL_REPORT_DESC_LEN");

/* clang-format on */

/**
 * @brief Whether a source index is valid.
 *
 * @param src Source index.
 * @return true if < HID_MERGE_SOURCES.
 */
static bool src_ok(unsigned src) {
    return src < HID_MERGE_SOURCES;
}

/** @copydoc hid_merge_init */
void hid_merge_init(hid_merge_t *m) {
    memset(m, 0, sizeof(*m));
}

/** @copydoc hid_merge_clear_source */
void hid_merge_clear_source(hid_merge_t *m, unsigned src) {
    if (src_ok(src)) {
        memset(&m->src[src], 0, sizeof(m->src[src]));
    }
}

/** @copydoc hid_merge_set_keyboard */
void hid_merge_set_keyboard(hid_merge_t *m, unsigned src, uint8_t modifiers, const uint8_t *keys, size_t nkeys) {
    if (!src_ok(src)) {
        return;
    }
    hid_source_state_t *s = &m->src[src];
    s->modifiers = modifiers;
    memset(s->keys, 0, sizeof(s->keys));
    size_t n = 0;
    for (size_t i = 0; keys != NULL && i < nkeys && n < HID_KBD_MAX_KEYS; i++) {
        /* 0 = no event; 0xE0..0xE7 are modifiers and live in the modifier byte, never in the key array. */
        if (keys[i] != 0 && (keys[i] < USAGE_MOD_FIRST || keys[i] > USAGE_MOD_LAST)) {
            s->keys[n++] = keys[i];
        }
    }
}

/** @copydoc hid_merge_set_consumer */
void hid_merge_set_consumer(hid_merge_t *m, unsigned src, uint16_t usage) {
    if (src_ok(src)) {
        m->src[src].consumer = (usage <= HID_CONSUMER_USAGE_MAX) ? usage : 0;
    }
}

/** @copydoc hid_merge_set_system */
void hid_merge_set_system(hid_merge_t *m, unsigned src, uint8_t bits) {
    if (src_ok(src)) {
        m->src[src].system = bits & (HID_SYS_SLEEP | HID_SYS_WAKEUP);
    }
}

/** @copydoc hid_merge_mouse */
void hid_merge_mouse(hid_merge_t *m, unsigned src, uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel,
                     int32_t pan) {
    if (!src_ok(src)) {
        return;
    }
    hid_source_state_t *s = &m->src[src];
    s->mouse_buttons = buttons & HID_MOUSE_BUTTON_MASK;
    s->axis[HID_AXIS_X] += dx;
    s->axis[HID_AXIS_Y] += dy;
    s->axis[HID_AXIS_WHEEL] += wheel;
    s->axis[HID_AXIS_PAN] += pan;
}

/** @copydoc hid_merge_get_keyboard */
void hid_merge_get_keyboard(const hid_merge_t *m, hid_kbd_report_t *out) {
    memset(out, 0, sizeof(*out));
    size_t n = 0;
    bool overflow = false;
    for (unsigned s = 0; s < HID_MERGE_SOURCES; s++) {
        out->modifiers |= m->src[s].modifiers;
        for (unsigned k = 0; k < HID_KBD_MAX_KEYS; k++) {
            uint8_t key = m->src[s].keys[k];
            if (key == 0) {
                continue;
            }
            bool dup = false;
            for (size_t i = 0; i < n; i++) {
                if (out->keys[i] == key) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            if (n < HID_KBD_MAX_KEYS) {
                out->keys[n++] = key;
            } else {
                overflow = true;
            }
        }
    }
    if (overflow) {
        /* More keys than slots: report the "phantom" state instead of silently dropping keys. */
        memset(out->keys, HID_KBD_ROLLOVER_ERROR, sizeof(out->keys));
    }
}

/** @copydoc hid_merge_get_consumer */
uint16_t hid_merge_get_consumer(const hid_merge_t *m) {
    for (unsigned s = 0; s < HID_MERGE_SOURCES; s++) {
        if (m->src[s].consumer != 0) {
            return m->src[s].consumer;
        }
    }
    return 0;
}

/** @copydoc hid_merge_get_system */
uint8_t hid_merge_get_system(const hid_merge_t *m) {
    uint8_t bits = 0;
    for (unsigned s = 0; s < HID_MERGE_SOURCES; s++) {
        bits |= m->src[s].system;
    }
    return bits;
}

/**
 * @brief Sum one axis over all sources, clamp it for the report and keep the remainder queued.
 *
 * Sources are merged as one: the remainder of a clamped sum is parked in source 0 so a fast flick that does
 * not fit one report is delivered by the next ones instead of being lost.
 *
 * @param m    Context.
 * @param axis Axis to drain.
 * @param lo   Lower clamp of the report field.
 * @param hi   Upper clamp of the report field.
 * @return Value to put in the report.
 */
static int32_t drain_axis(hid_merge_t *m, hid_axis_t axis, int32_t lo, int32_t hi) {
    int64_t sum = 0;
    for (unsigned s = 0; s < HID_MERGE_SOURCES; s++) {
        sum += m->src[s].axis[axis];
        m->src[s].axis[axis] = 0;
    }
    int64_t sent = sum < lo ? lo : (sum > hi ? hi : sum);
    int64_t rest = sum - sent;
    m->src[0].axis[axis] = (int32_t)(rest > INT32_MAX ? INT32_MAX : (rest < INT32_MIN ? INT32_MIN : rest));
    return (int32_t)sent;
}

/** @copydoc hid_merge_take_mouse */
bool hid_merge_take_mouse(hid_merge_t *m, hid_mouse_in_t *out) {
    memset(out, 0, sizeof(*out));
    for (unsigned s = 0; s < HID_MERGE_SOURCES; s++) {
        out->buttons |= m->src[s].mouse_buttons;
    }
    out->x = (int16_t)drain_axis(m, HID_AXIS_X, INT16_MIN + 1, INT16_MAX);
    out->y = (int16_t)drain_axis(m, HID_AXIS_Y, INT16_MIN + 1, INT16_MAX);
    out->wheel = (int8_t)drain_axis(m, HID_AXIS_WHEEL, -127, 127);
    out->pan = (int8_t)drain_axis(m, HID_AXIS_PAN, -127, 127);
    return out->x != 0 || out->y != 0 || out->wheel != 0 || out->pan != 0;
}

/** @copydoc hid_merge_mouse_pending */
bool hid_merge_mouse_pending(const hid_merge_t *m) {
    for (unsigned s = 0; s < HID_MERGE_SOURCES; s++) {
        const hid_source_state_t *st = &m->src[s];
        for (unsigned a = 0; a < HID_AXIS_COUNT; a++) {
            if (st->axis[a] != 0) {
                return true;
            }
        }
    }
    return false;
}
