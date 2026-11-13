/**
 * @file combo.c
 * @brief Hold-combo detection (see combo.h).
 *
 * Dependencies: libc only. Threading: single task.
 */
#include "combo.h"

#include <stddef.h>

/** Candidate phases. */
enum { PHASE_IDLE = 0, PHASE_CTRL_ALT = 1, PHASE_CTRL_ALT_ESC = 2 };

/** @copydoc combo_init */
void combo_init(combo_t *c, uint32_t ap_hold_ms) {
    c->ap_hold_ms = ap_hold_ms ? ap_hold_ms : COMBO_DEFAULT_AP_HOLD_MS;
    c->since_ms = 0;
    c->phase = PHASE_IDLE;
    c->ap_fired = false;
    c->recovery_fired = false;
}

/**
 * @brief Classify the keyboard state.
 *
 * @param modifiers Merged modifiers.
 * @param keys      Merged keys.
 * @param nkeys     Entries in @p keys.
 * @return PHASE_CTRL_ALT (a Ctrl and an Alt, no key), PHASE_CTRL_ALT_ESC (the same plus only Esc) or PHASE_IDLE. Any
 *         other modifier (Shift, GUI) or key makes it idle: the combo must be pressed on its own. Ctrl and Alt are
 *         harmless on their own (no auto-repeat, no character), unlike a typed key.
 */
static uint8_t classify(uint8_t modifiers, const uint8_t *keys, unsigned nkeys) {
    const uint8_t ctrl = COMBO_MOD_LCTRL | COMBO_MOD_RCTRL;
    const uint8_t alt = COMBO_MOD_LALT | COMBO_MOD_RALT;
    if ((modifiers & ctrl) == 0 || (modifiers & alt) == 0 || (modifiers & (uint8_t)~(ctrl | alt)) != 0) {
        return PHASE_IDLE;
    }
    unsigned pressed = 0;
    bool esc = false;
    for (unsigned i = 0; keys != NULL && i < nkeys; i++) {
        if (keys[i] == 0) {
            continue;
        }
        pressed++;
        esc = keys[i] == COMBO_KEY_ESC;
    }
    if (pressed == 0) {
        return PHASE_CTRL_ALT;
    }
    return (pressed == 1 && esc) ? PHASE_CTRL_ALT_ESC : PHASE_IDLE;
}

/** @copydoc combo_update */
combo_event_t combo_update(combo_t *c, uint32_t now_ms, uint8_t modifiers, const uint8_t *keys, unsigned nkeys) {
    uint8_t phase = classify(modifiers, keys, nkeys);
    if (phase == PHASE_IDLE) {
        c->phase = PHASE_IDLE;
        c->ap_fired = false;
        c->recovery_fired = false;
        return COMBO_NONE;
    }
    if (phase != c->phase) {
        /* Entering Esc from the Ctrl + Alt phase keeps the original start: they have been held all along, and
         * the 10 s recovery time is counted from when they went down. */
        if (!(c->phase == PHASE_CTRL_ALT && phase == PHASE_CTRL_ALT_ESC)) {
            c->since_ms = now_ms;
            c->ap_fired = false;
            c->recovery_fired = false;
        }
        c->phase = phase;
    }
    uint32_t held = now_ms - c->since_ms; /* unsigned subtraction: safe across wrap-around */
    if (!c->ap_fired && held >= c->ap_hold_ms && phase == PHASE_CTRL_ALT) {
        c->ap_fired = true;
        return COMBO_AP_ON;
    }
    if (phase == PHASE_CTRL_ALT_ESC && !c->recovery_fired && held >= COMBO_RECOVERY_HOLD_MS) {
        c->recovery_fired = true;
        return COMBO_RECOVERY;
    }
    return COMBO_NONE;
}

/** @copydoc combo_mouse_init */
void combo_mouse_init(combo_mouse_t *c, uint32_t hold_ms) {
    c->hold_ms = hold_ms ? hold_ms : COMBO_DEFAULT_AP_HOLD_MS;
    c->since_ms = 0;
    c->active = false;
    c->fired = false;
}

/** @copydoc combo_mouse_update */
bool combo_mouse_update(combo_mouse_t *c, uint32_t now_ms, uint8_t buttons) {
    const bool both_only = buttons == (COMBO_BTN_LEFT | COMBO_BTN_RIGHT);
    if (!both_only) {
        c->active = false;
        c->fired = false;
        return false;
    }
    if (!c->active) {
        c->active = true;
        c->since_ms = now_ms;
    }
    if (!c->fired && now_ms - c->since_ms >= c->hold_ms) {
        c->fired = true;
        return true;
    }
    return false;
}

/** @copydoc combo_button_init */
void combo_button_init(combo_button_t *c) {
    c->since_ms = 0;
    c->down = false;
}

/** @copydoc combo_button_update */
bool combo_button_update(combo_button_t *c, uint32_t now_ms, bool pressed) {
    if (pressed) {
        if (!c->down) {
            c->down = true;
            c->since_ms = now_ms;
        }
        return false;
    }
    if (!c->down) {
        return false;
    }
    c->down = false;
    const uint32_t held = now_ms - c->since_ms;
    return held >= COMBO_BUTTON_MIN_MS && held <= COMBO_BUTTON_MAX_MS;
}
