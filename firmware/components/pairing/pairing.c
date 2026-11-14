/**
 * @file pairing.c
 * @brief Pairing state machine and challenge logic (see pairing.h).
 *
 * Dependencies: libc only. Threading: single owner.
 */
#include "pairing.h"

#include <string.h>

/** Mouse button bits used by the click challenge (match hid_mouse_button_t). */
#define BTN_LEFT   0x01
#define BTN_RIGHT  0x02
#define BTN_MIDDLE 0x04
/** Alphabet of the keyboard code: no I, O, 0, 1 (easy to confuse). 32 symbols so rng % 32 has no bias. */
static const char CODE_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
/** Modulus of the passkey (10^6). */
#define PASSKEY_MOD 1000000u

/** @copydoc pairing_init */
void pairing_init(pairing_t *p, bool require_confirm) {
    memset(p, 0, sizeof(*p));
    p->state = PAIR_IDLE;
    p->require_confirm = require_confirm;
}

/** @copydoc pairing_active */
bool pairing_active(const pairing_t *p) {
    return p->state >= PAIR_SCANNING && p->state <= PAIR_CONFIRM;
}

/** @copydoc pairing_lock_remaining_s */
uint32_t pairing_lock_remaining_s(const pairing_t *p, uint32_t now_ms) {
    if (!p->locked) {
        return 0;
    }
    uint32_t left = p->locked_until_ms - now_ms;
    return (left > PAIR_LOCK_MS) ? 0 : (left + 999) / 1000; /* wrap-safe: an "expired" lock reads as huge */
}

/** @copydoc pairing_remaining_ms */
uint32_t pairing_remaining_ms(const pairing_t *p, uint32_t now_ms) {
    if (p->phase_timeout_ms == 0) {
        return 0;
    }
    uint32_t used = now_ms - p->phase_start_ms;
    return used >= p->phase_timeout_ms ? 0 : p->phase_timeout_ms - used;
}

/**
 * @brief Enter a timed phase.
 *
 * @param p       Machine.
 * @param st      New state.
 * @param now_ms  Current time.
 * @param timeout Phase duration in ms (0 = untimed).
 */
static void enter(pairing_t *p, pair_state_t st, uint32_t now_ms, uint32_t timeout) {
    p->state = st;
    p->phase_start_ms = now_ms;
    p->phase_timeout_ms = timeout;
}

/**
 * @brief Whether a new pairing may start now.
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @return PAIR_START_OK, _BUSY or _LOCKED. Also lifts an expired lock.
 */
static pair_start_t can_start(pairing_t *p, uint32_t now_ms) {
    if (p->locked && pairing_lock_remaining_s(p, now_ms) == 0) {
        p->locked = false;
        p->failures = 0;
    }
    if (p->locked) {
        return PAIR_START_LOCKED;
    }
    return pairing_active(p) ? PAIR_START_BUSY : PAIR_START_OK;
}

/** @copydoc pairing_begin_scan */
pair_start_t pairing_begin_scan(pairing_t *p, uint32_t now_ms) {
    pair_start_t r = can_start(p, now_ms);
    if (r == PAIR_START_OK) {
        p->fail = FAIL_NONE;
        p->level = SEC_UNKNOWN;
        enter(p, PAIR_SCANNING, now_ms, 0);
    }
    return r;
}

/** @copydoc pairing_scan_done */
void pairing_scan_done(pairing_t *p) {
    if (p->state == PAIR_SCANNING) {
        p->state = PAIR_IDLE;
        p->phase_timeout_ms = 0;
    }
}

/** @copydoc pairing_begin_pair */
pair_start_t pairing_begin_pair(pairing_t *p, uint32_t now_ms, bool allow_legacy, bool have_other_keyboard) {
    /* A scan in progress may be turned into a pairing; anything else active is busy. */
    pair_state_t before = p->state;
    if (before == PAIR_SCANNING) {
        p->state = PAIR_IDLE;
    }
    pair_start_t r = can_start(p, now_ms);
    if (r != PAIR_START_OK) {
        p->state = before;
        return r;
    }
    memset(&p->challenge, 0, sizeof(p->challenge));
    p->fail = FAIL_NONE;
    p->level = SEC_UNKNOWN;
    p->authenticated = false;
    p->has_passkey = false;
    p->allow_legacy = allow_legacy;
    p->need_confirm = p->require_confirm && have_other_keyboard;
    enter(p, PAIR_CONNECTING, now_ms, PAIR_PAIRING_TIMEOUT_MS);
    return PAIR_START_OK;
}

/** @copydoc pairing_on_connected */
void pairing_on_connected(pairing_t *p, uint32_t now_ms) {
    (void)now_ms; /* The 60 s window started at CONNECTING and covers connect + SMP + discovery. */
    if (p->state == PAIR_CONNECTING) {
        p->state = PAIR_PAIRING;
    }
}

/** @copydoc pairing_make_passkey */
uint32_t pairing_make_passkey(pairing_t *p, pair_rng_t rng) {
    if (p->state != PAIR_PAIRING) {
        return 0;
    }
    p->passkey = rng() % PASSKEY_MOD;
    p->has_passkey = true;
    return p->passkey;
}

/** @copydoc pairing_fail */
void pairing_fail(pairing_t *p, uint32_t now_ms, pair_fail_t reason) {
    if (!pairing_active(p) || p->state == PAIR_SCANNING) {
        return;
    }
    p->state = PAIR_FAILED;
    p->fail = reason;
    p->has_passkey = false;
    p->phase_timeout_ms = 0;
    memset(&p->challenge, 0, sizeof(p->challenge));
    if (reason != FAIL_CANCELLED && reason != FAIL_NONE) {
        /* Rate limiting (CLAUDE.md §3.12): the third consecutive failure locks pairing for 60 s. */
        if (++p->failures >= PAIR_MAX_FAILURES) {
            p->locked = true;
            p->locked_until_ms = now_ms + PAIR_LOCK_MS;
        }
    }
}

/** @copydoc pairing_cancel */
void pairing_cancel(pairing_t *p) {
    if (p->state == PAIR_SCANNING) {
        pairing_scan_done(p);
    } else {
        pairing_fail(p, 0, FAIL_CANCELLED);
    }
}

/** @copydoc pairing_on_secured */
bool pairing_on_secured(pairing_t *p, uint32_t now_ms, bool authenticated, bool secure_conn) {
    if (p->state != PAIR_PAIRING) {
        return false;
    }
    if (!secure_conn && !p->allow_legacy) {
        pairing_fail(p, now_ms, FAIL_LEGACY);
        return false;
    }
    p->authenticated = authenticated;
    p->has_passkey = false;
    if (!secure_conn) {
        p->level = SEC_RED;
    } else {
        p->level = authenticated ? SEC_GREEN : SEC_YELLOW;
    }
    return true;
}

/**
 * @brief Finish the verification: go to CONFIRM if needed, else BONDED.
 *
 * @param p      Machine.
 * @param now_ms Current time.
 */
static void verified(pairing_t *p, uint32_t now_ms) {
    memset(&p->challenge, 0, sizeof(p->challenge));
    if (p->need_confirm) {
        enter(p, PAIR_CONFIRM, now_ms, PAIR_CONFIRM_TIMEOUT_MS);
    } else {
        p->state = PAIR_BONDED;
        p->phase_timeout_ms = 0;
        p->failures = 0;
    }
}

/**
 * @brief Pick a random character from a set.
 *
 * @param rng Random source.
 * @param n   Set size.
 * @return Index in [0, n).
 */
static unsigned pick(pair_rng_t rng, unsigned n) {
    return rng() % n;
}

/** @copydoc pairing_on_hid_ready */
void pairing_on_hid_ready(pairing_t *p, uint32_t now_ms, bool is_keyboard, pair_rng_t rng) {
    if (p->state != PAIR_PAIRING || p->level == SEC_UNKNOWN) {
        return;
    }
    if (p->authenticated) {
        verified(p, now_ms); /* Passkey entry already proved physical access to the device. */
        return;
    }
    challenge_t *c = &p->challenge;
    memset(c, 0, sizeof(*c));
    if (is_keyboard) {
        c->kind = CHALLENGE_KEYBOARD;
        c->len = CHALLENGE_CODE_LEN;
        for (unsigned i = 0; i < CHALLENGE_CODE_LEN; i++) {
            c->text[i] = CODE_ALPHABET[pick(rng, sizeof(CODE_ALPHABET) - 1)];
        }
    } else {
        static const char BUTTONS[3] = {'L', 'R', 'M'};
        c->kind = CHALLENGE_MOUSE;
        c->len = CHALLENGE_MOUSE_LEN;
        for (unsigned i = 0; i < CHALLENGE_MOUSE_LEN; i++) {
            c->text[i] = BUTTONS[pick(rng, 3)];
        }
        if (c->text[0] == c->text[1] && c->text[1] == c->text[2] && c->text[2] == c->text[3] &&
            c->text[3] == c->text[4] && c->text[4] == c->text[5]) {
            c->text[5] = (c->text[5] == 'L') ? 'R' : ((c->text[5] == 'R') ? 'M' : 'L'); /* never all the same button */
        }
    }
    c->text[c->len] = '\0';
    enter(p, PAIR_QUARANTINE, now_ms, PAIR_CHALLENGE_TIMEOUT_MS);
}

/**
 * @brief Apply one challenge input character.
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @param ch     The character the user produced.
 */
static void challenge_input(pairing_t *p, uint32_t now_ms, char ch) {
    challenge_t *c = &p->challenge;
    if (c->text[c->pos] == ch) {
        if (++c->pos >= c->len) {
            verified(p, now_ms);
        }
        return;
    }
    c->pos = 0; /* A wrong input restarts the sequence. */
    if (++c->mistakes >= CHALLENGE_MAX_MISTAKES) {
        pairing_fail(p, now_ms, FAIL_CHALLENGE);
    }
}

/** @copydoc pairing_input_buttons */
void pairing_input_buttons(pairing_t *p, uint32_t now_ms, uint8_t buttons) {
    if (p->state != PAIR_QUARANTINE || p->challenge.kind != CHALLENGE_MOUSE) {
        return;
    }
    uint8_t pressed = (uint8_t)(buttons & ~p->challenge.prev_buttons); /* rising edges only */
    p->challenge.prev_buttons = buttons;
    static const struct {
        uint8_t bit; /**< Mouse button bit (BTN_*). */
        char ch;     /**< Letter shown in the challenge for that button. */
    } MAP[] = {{BTN_LEFT, 'L'}, {BTN_RIGHT, 'R'}, {BTN_MIDDLE, 'M'}};
    for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]) && p->state == PAIR_QUARANTINE; i++) {
        if (pressed & MAP[i].bit) {
            challenge_input(p, now_ms, MAP[i].ch);
        }
    }
}

/** @copydoc challenge_usage_to_char */
char challenge_usage_to_char(uint8_t usage) {
    if (usage >= 0x04 && usage <= 0x1D) {
        return (char)('A' + (usage - 0x04));
    }
    if (usage >= 0x1E && usage <= 0x26) {
        return (char)('1' + (usage - 0x1E));
    }
    return usage == 0x27 ? '0' : 0;
}

/** @copydoc pairing_input_keys */
void pairing_input_keys(pairing_t *p, uint32_t now_ms, const uint8_t keys[6]) {
    if (p->state != PAIR_QUARANTINE || p->challenge.kind != CHALLENGE_KEYBOARD) {
        return;
    }
    uint8_t prev[6];
    memcpy(prev, p->challenge.prev_keys, sizeof(prev));
    memcpy(p->challenge.prev_keys, keys, sizeof(prev));
    for (unsigned i = 0; i < 6 && p->state == PAIR_QUARANTINE; i++) {
        bool is_new = keys[i] != 0;
        for (unsigned j = 0; j < 6 && is_new; j++) {
            is_new = prev[j] != keys[i]; /* only keys that were not down in the previous report */
        }
        char ch = is_new ? challenge_usage_to_char(keys[i]) : 0;
        if (ch != 0) {
            challenge_input(p, now_ms, ch);
        }
    }
}

/** @copydoc pairing_confirm */
void pairing_confirm(pairing_t *p, uint32_t now_ms) {
    if (p->state == PAIR_CONFIRM) {
        p->state = PAIR_BONDED;
        p->phase_timeout_ms = 0;
        p->failures = 0;
    }
    (void)now_ms;
}

/** @copydoc pairing_tick */
void pairing_tick(pairing_t *p, uint32_t now_ms) {
    if (p->locked && pairing_lock_remaining_s(p, now_ms) == 0) {
        p->locked = false;
        p->failures = 0;
    }
    if (pairing_active(p) && p->state != PAIR_SCANNING && p->phase_timeout_ms != 0 &&
        pairing_remaining_ms(p, now_ms) == 0) {
        pairing_fail(p, now_ms, FAIL_TIMEOUT);
    }
}
