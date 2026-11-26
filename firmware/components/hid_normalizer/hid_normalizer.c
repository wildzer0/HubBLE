/**
 * @file hid_normalizer.c
 * @brief Report decoding to normalized events (see hid_normalizer.h).
 *
 * Dependencies: hid_parser, hid_report. Threading: stateless.
 */
#include "hid_normalizer.h"

#include <stdint.h>

#include <string.h>

/** Generic Desktop usages the mouse decoder needs. */
#define USAGE_X             0x30
#define USAGE_Y             0x31
#define USAGE_WHEEL         0x38
#define USAGE_SYSTEM_SLEEP  0x82
#define USAGE_SYSTEM_WAKEUP 0x83
/** Digitizer usages for touchpad fingers. */
#define USAGE_TIP_SWITCH 0x42
#define USAGE_CONTACT_ID 0x51

/** Touchpad finger tracking while one report is decoded: fields come in report order, finger by finger. */
typedef struct {
    bool tip_seen;  /**< The report has Tip Switch fields (a touchpad): only touching fingers give a position. */
    bool cur_tip;   /**< The current finger is touching. */
    int finger;     /**< Index of the current finger (counted by Tip Switch usages). */
    int added;      /**< Finger that owns the last entry of ev->contacts, -2 if none. */
    int32_t cur_id; /**< Contact ID of the current finger. */
} touch_ctx_t;
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
 * @brief Saturating 32-bit addition.
 *
 * A hostile report map can declare many axis fields of the same usage with 32-bit values, so summing them must not
 * overflow (signed overflow is undefined behaviour). Found by the fuzzer.
 *
 * @param a First addend.
 * @param b Second addend.
 * @return a + b clamped to the int32_t range.
 */
static int32_t sat_add(int32_t a, int32_t b) {
    const int64_t r = (int64_t)a + b;
    return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}

/**
 * @brief Decode Generic Desktop fields: pointer axes, wheel and system control.
 *
 * @param f    Field.
 * @param data Payload.
 * @param len  Payload length.
 * @param ev   Event to update.
 * @param tc   Touchpad finger tracking for this report.
 */
static void decode_desktop(const hid_field_t *f, const uint8_t *data, size_t len, hid_norm_event_t *ev,
                           touch_ctx_t *tc) {
    for (unsigned i = 0; i < f->count; i++) {
        int32_t v;
        if (!hid_field_value(f, i, data, len, &v)) {
            break;
        }
        uint16_t usage = hid_field_usage(f, i);
        switch (usage) {
        case USAGE_X:
        case USAGE_Y: {
            ev->has_mouse = true;
            if (f->flags & HID_FLAG_RELATIVE) {
                ev->rel_xy = true;
                int32_t *axis = usage == USAGE_X ? &ev->dx : &ev->dy;
                *axis = sat_add(*axis, v);
            } else { /* A position, never a movement: hid_norm_abs_to_rel() turns it into one. */
                if (tc->tip_seen && !tc->cur_tip) {
                    break; /* Touchpad finger slot that is not touching. */
                }
                if (tc->added != tc->finger || ev->ncontacts == 0) { /* First axis of this finger: a new contact. */
                    if (ev->ncontacts >= HID_MAX_CONTACTS) {
                        break;
                    }
                    ev->contacts[ev->ncontacts++] = (hid_contact_t){.id = tc->cur_id};
                    tc->added = tc->finger;
                }
                ev->abs_xy = true;
                hid_contact_t *c = &ev->contacts[ev->ncontacts - 1];
                *(usage == USAGE_X ? &c->x : &c->y) = v;
            }
            break;
        }
        case USAGE_WHEEL:
            ev->has_mouse = true;
            ev->wheel = sat_add(ev->wheel, v);
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
    touch_ctx_t tc = {.finger = -1, .added = -2};
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
                        ev->pan = sat_add(ev->pan, v);
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
            decode_desktop(f, data, len, ev, &tc);
            any = true;
            break;
        case HID_PAGE_DIGITIZER:
            for (unsigned i = 0; i < f->count; i++) {
                int32_t v;
                if (!hid_field_value(f, i, data, len, &v)) {
                    break;
                }
                const uint16_t usage = hid_field_usage(f, i);
                if (usage == USAGE_TIP_SWITCH) {
                    tc.tip_seen = true;
                    tc.cur_tip = v != 0;
                    tc.finger++;
                    tc.cur_id = 0;
                } else if (usage == USAGE_CONTACT_ID) {
                    tc.cur_id = v;
                    if (tc.added == tc.finger && ev->ncontacts > 0) {
                        ev->contacts[ev->ncontacts - 1].id = v; /* Contact ID after the axes. */
                    }
                }
            }
            break;
        default:
            break; /* Vendor pages and anything unknown are ignored. */
        }
    }
    ev->touch_report = tc.tip_seen;
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

/** @copydoc hid_norm_is_pointer */
bool hid_norm_is_pointer(const hid_layout_t *layout) {
    for (unsigned n = 0; n < layout->nfields; n++) {
        const hid_field_t *f = &layout->fields[n];
        if (f->dir != HID_REPORT_IN || f->usage_page != HID_PAGE_GENERIC_DESKTOP) {
            continue;
        }
        for (unsigned i = 0; i < f->count; i++) {
            const uint16_t usage = hid_field_usage(f, i);
            if (usage == USAGE_X || usage == USAGE_Y) {
                return true;
            }
        }
    }
    return false;
}

/** GAP appearances of HID devices (Bluetooth Assigned Numbers, category 0x0F). */
#define APPEARANCE_KEYBOARD  0x03C1
#define APPEARANCE_MOUSE     0x03C2
#define APPEARANCE_DIGITIZER 0x03C5
#define APPEARANCE_TOUCHPAD  0x03C9

/** Generic Desktop usages of the top-level collections that tell what a device is. */
#define USAGE_POINTER_COLLECTION  0x01
#define USAGE_MOUSE_COLLECTION    0x02
#define USAGE_KEYBOARD_COLLECTION 0x06

/** First usage of the modifier keys (Left Control). */
#define USAGE_FIRST_MODIFIER 0xE0

/** @copydoc hid_norm_is_keyboard */
bool hid_norm_is_keyboard(const hid_layout_t *layout) {
    bool has_keys = false;
    bool has_modifiers = false;
    for (unsigned n = 0; n < layout->nfields; n++) {
        const hid_field_t *f = &layout->fields[n];
        if (f->dir != HID_REPORT_IN || f->usage_page != HID_PAGE_KEYBOARD) {
            continue;
        }
        for (unsigned i = 0; i < f->count; i++) {
            const uint16_t usage = (f->flags & HID_FLAG_VARIABLE) ? hid_field_usage(f, i) : 0;
            if ((f->flags & HID_FLAG_VARIABLE) == 0 || usage < USAGE_FIRST_MODIFIER) {
                has_keys = true; /* A key array, or a bitmap of ordinary keys. */
            } else {
                has_modifiers = true;
            }
        }
    }
    if (!has_keys) {
        return false;
    }
    if (!hid_norm_is_pointer(layout)) {
        return true;
    }
    /* Both a keyboard and a pointer collection: mice and trackballs often declare one for shortcut keys (even with
     * modifiers) and some keyboards declare a mouse. What the device is "mainly" is its first top-level collection;
     * without that information require a modifier byte as well. */
    if (layout->primary_page == HID_PAGE_GENERIC_DESKTOP) {
        if (layout->primary_usage == USAGE_KEYBOARD_COLLECTION) {
            return true;
        }
        if (layout->primary_usage == USAGE_MOUSE_COLLECTION || layout->primary_usage == USAGE_POINTER_COLLECTION) {
            return false;
        }
    }
    return has_modifiers;
}

/** @copydoc hid_norm_pairing_is_keyboard */
bool hid_norm_pairing_is_keyboard(uint16_t appearance, const hid_layout_t *layout) {
    switch (appearance) {
    case APPEARANCE_KEYBOARD:
        return true;
    case APPEARANCE_MOUSE:
    case APPEARANCE_DIGITIZER:
    case APPEARANCE_TOUCHPAD:
        return false;
    default:
        return hid_norm_is_keyboard(layout);
    }
}

/**
 * @brief Saturating subtraction.
 *
 * @param a Minuend.
 * @param b Subtrahend.
 * @return a - b clamped to the int32_t range.
 */
static int32_t sat_sub(int32_t a, int32_t b) {
    const int64_t r = (int64_t)a - b;
    return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}

/**
 * @brief |dx| + |dy|, saturated.
 *
 * @param dx X movement.
 * @param dy Y movement.
 * @return The Manhattan length, at most INT32_MAX.
 */
static int32_t manhattan(int32_t dx, int32_t dy) {
    const int64_t r = (dx < 0 ? -(int64_t)dx : dx) + (dy < 0 ? -(int64_t)dy : dy);
    return r > INT32_MAX ? INT32_MAX : (int32_t)r;
}

/** @copydoc hid_norm_abs_to_rel */
bool hid_norm_abs_to_rel(hid_abs_state_t *st, hid_norm_event_t *ev, uint32_t now_ms) {
    if (ev->rel_xy) {
        return false;
    }
    if (!ev->abs_xy) {
        if (!ev->touch_report || !st->valid) {
            return false;
        }
        /* Touchpad report with no finger down: the touch ended. */
        const bool tap = !st->moving && !st->pressed && !st->multi && ev->buttons == 0;
        st->valid = false;
        st->tap_recent = tap;
        st->tap_end_ms = now_ms;
        return tap;
    }
    int32_t dx = 0, dy = 0;
    if (!st->valid || (uint32_t)(now_ms - st->last_ms) > HID_ABS_TOUCH_GAP_MS) { /* A new touch. */
        st->drag = st->tap_recent && (uint32_t)(now_ms - st->tap_end_ms) <= HID_TAP_DRAG_MS;
        st->tap_recent = false;
        st->down_ms = now_ms;
        st->travel = 0;
        st->held_dx = 0;
        st->held_dy = 0;
        st->moving = !ev->touch_report; /* Only touchpads tap: other absolute pointers move at once. */
        st->lift_hold = false;
        st->last_move_ms = now_ms;
        st->rem_x = 0;
        st->rem_y = 0;
        st->pressed = false;
        st->multi = false;
    } else { /* The finger that moved most since the previous report. */
        int32_t best = -1;
        for (unsigned i = 0; i < ev->ncontacts; i++) {
            const hid_contact_t *c = &ev->contacts[i];
            for (unsigned k = 0; k < st->nprev; k++) {
                const hid_contact_t *p = &st->prev[k];
                if (p->id != c->id && !(ev->ncontacts == 1 && st->nprev == 1)) {
                    continue;
                }
                const int32_t mx = sat_sub(c->x, p->x), my = sat_sub(c->y, p->y), m = manhattan(mx, my);
                if (m > best) {
                    best = m;
                    dx = mx;
                    dy = my;
                }
                break;
            }
        }
    }
    st->valid = true;
    st->nprev = ev->ncontacts;
    memcpy(st->prev, ev->contacts, sizeof(st->prev));
    st->last_ms = now_ms;
    st->multi |= ev->ncontacts > 1;
    st->pressed |= ev->buttons != 0;
    if (ev->touch_report && ev->buttons != 0 && ev->ncontacts < 2) {
        dx = 0; /* Clickpad pressed by one finger: it flattens, which must not move what is being clicked. */
        dy = 0;
    }
    if (!st->moving) { /* Could still be a tap: hold the movement back. */
        st->held_dx = sat_add(st->held_dx, dx);
        st->held_dy = sat_add(st->held_dy, dy);
        st->travel = manhattan(st->held_dx, st->held_dy) > st->travel ? manhattan(st->held_dx, st->held_dy)
                                                                      : st->travel;
        st->moving = st->travel > HID_TAP_MAX_TRAVEL || (uint32_t)(now_ms - st->down_ms) > HID_TAP_MAX_MS;
        dx = st->moving ? st->held_dx : 0;
        dy = st->moving ? st->held_dy : 0;
        if (st->moving) {
            st->held_dx = 0;
            st->held_dy = 0;
            st->last_move_ms = now_ms;
        }
    } else if (ev->touch_report) { /* Lift roll: hold movement that follows a rest, drop it if the finger lifts. */
        const bool moved = manhattan(dx, dy) > 1;
        if (!st->lift_hold && moved && (uint32_t)(now_ms - st->last_move_ms) >= HID_REST_MS) {
            st->lift_hold = true;
            st->lift_hold_ms = now_ms;
        }
        if (moved) {
            st->last_move_ms = now_ms;
        }
        if (st->lift_hold) {
            st->held_dx = sat_add(st->held_dx, dx);
            st->held_dy = sat_add(st->held_dy, dy);
            const bool real = (uint32_t)(now_ms - st->lift_hold_ms) >= HID_LIFT_HOLD_MS ||
                              manhattan(st->held_dx, st->held_dy) > HID_LIFT_MAX_TRAVEL;
            dx = real ? st->held_dx : 0;
            dy = real ? st->held_dy : 0;
            if (real) {
                st->lift_hold = false;
                st->held_dx = 0;
                st->held_dy = 0;
            }
        }
    }
    if (ev->touch_report) { /* Speed: scale, carrying the remainder so slow moves are not lost. */
        const int64_t sx = (int64_t)dx * HID_TOUCHPAD_SPEED_NUM + st->rem_x;
        const int64_t sy = (int64_t)dy * HID_TOUCHPAD_SPEED_NUM + st->rem_y;
        dx = (int32_t)(sx / HID_TOUCHPAD_SPEED_DEN);
        dy = (int32_t)(sy / HID_TOUCHPAD_SPEED_DEN);
        st->rem_x = (int32_t)(sx % HID_TOUCHPAD_SPEED_DEN);
        st->rem_y = (int32_t)(sy % HID_TOUCHPAD_SPEED_DEN);
    }
    ev->dx = dx;
    ev->dy = dy;
    if (st->drag && st->moving) {
        ev->buttons |= 1u; /* Tap-and-drag: left button held until the finger lifts. */
    }
    return false;
}
