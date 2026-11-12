/**
 * @file combo.c
 * @brief Hold-combo detection (see combo.h).
 *
 * Dependencies: libc only. Threading: single task.
 */
#include "combo.h"

#include <stddef.h>

/** Candidate phases. */
enum { PHASE_IDLE = 0, PHASE_SHIFTS = 1, PHASE_SHIFTS_ESC = 2 };

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
 * @return PHASE_SHIFTS (both Shifts, no key), PHASE_SHIFTS_ESC (both Shifts + only Esc) or PHASE_IDLE. Any
 *         other modifier or key makes it idle: the combo must be pressed on its own.
 */
static uint8_t classify(uint8_t modifiers, const uint8_t *keys, unsigned nkeys) {
    if (modifiers != (COMBO_MOD_LSHIFT | COMBO_MOD_RSHIFT)) {
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
        return PHASE_SHIFTS;
    }
    return (pressed == 1 && esc) ? PHASE_SHIFTS_ESC : PHASE_IDLE;
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
        /* Entering Esc from the Shift phase keeps the original start: the Shifts have been held all along, and
         * the 10 s recovery time is counted from when the Shifts went down. */
        if (!(c->phase == PHASE_SHIFTS && phase == PHASE_SHIFTS_ESC)) {
            c->since_ms = now_ms;
            c->ap_fired = false;
            c->recovery_fired = false;
        }
        c->phase = phase;
    }
    uint32_t held = now_ms - c->since_ms; /* unsigned subtraction: safe across wrap-around */
    if (!c->ap_fired && held >= c->ap_hold_ms && phase == PHASE_SHIFTS) {
        c->ap_fired = true;
        return COMBO_AP_ON;
    }
    if (phase == PHASE_SHIFTS_ESC && !c->recovery_fired && held >= COMBO_RECOVERY_HOLD_MS) {
        c->recovery_fired = true;
        return COMBO_RECOVERY;
    }
    return COMBO_NONE;
}
