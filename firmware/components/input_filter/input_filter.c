/**
 * @file input_filter.c
 * @brief Anti-chatter and hold-to-register filter (see input_filter.h).
 *
 * Dependencies: libc only. Threading: none.
 */
#include "input_filter.h"

#include <string.h>

/** @copydoc input_filter_init */
void input_filter_init(input_filter_t *f, uint32_t bounce_ms, uint32_t hold_ms) {
    memset(f, 0, sizeof(*f));
    f->bounce_ms = bounce_ms;
    f->hold_ms = hold_ms;
}

/**
 * @brief Find the tracked entry of an element, or NULL.
 *
 * @param f Filter state.
 * @param id Element id (key usage or button bit).
 * @return The entry, or NULL.
 */
static input_filter_entry_t *find(input_filter_t *f, uint8_t id) {
    for (size_t i = 0; i < INPUT_FILTER_MAX; i++) {
        if (f->e[i].state != IF_FREE && f->e[i].id == id) {
            return &f->e[i];
        }
    }
    return NULL;
}

/**
 * @brief A free entry; an old RELEASED one is recycled when the table is full. NULL if none.
 *
 * @param f Filter state.
 * @return A free entry, or NULL.
 */
static input_filter_entry_t *allocate(input_filter_t *f) {
    input_filter_entry_t *oldest = NULL;
    for (size_t i = 0; i < INPUT_FILTER_MAX; i++) {
        if (f->e[i].state == IF_FREE) {
            return &f->e[i];
        }
        if (f->e[i].state == IF_RELEASED && (oldest == NULL || f->e[i].since_ms < oldest->since_ms)) {
            oldest = &f->e[i];
        }
    }
    return oldest;
}

/**
 * @brief Promote pending elements whose hold time has elapsed and drop expired releases.
 *
 * @param f Filter state.
 * @param now_ms Current time in ms.
 */
static void advance(input_filter_t *f, uint32_t now_ms) {
    for (size_t i = 0; i < INPUT_FILTER_MAX; i++) {
        input_filter_entry_t *e = &f->e[i];
        if (e->state == IF_PENDING && e->present && now_ms - e->since_ms >= f->hold_ms) {
            e->state = IF_ACTIVE;
        } else if (e->state == IF_RELEASED && now_ms - e->since_ms >= f->bounce_ms) {
            e->state = IF_FREE;
        }
    }
}

/** @copydoc input_filter_update */
void input_filter_update(input_filter_t *f, uint32_t now_ms, const uint8_t *ids, size_t n) {
    for (size_t i = 0; i < INPUT_FILTER_MAX; i++) {
        f->e[i].present = false;
    }
    for (size_t i = 0; i < n; i++) {
        if (ids[i] == 0) {
            continue;
        }
        input_filter_entry_t *e = find(f, ids[i]);
        if (e == NULL) {
            e = allocate(f);
            if (e == NULL) {
                continue; /* Table full of live elements: cannot happen with a real keyboard. */
            }
            e->id = ids[i];
            e->present = true;
            if (f->hold_ms == 0) {
                e->state = IF_ACTIVE;
            } else {
                e->state = IF_PENDING;
                e->since_ms = now_ms;
            }
            continue;
        }
        e->present = true;
        if (e->state == IF_RELEASED) {
            if (now_ms - e->since_ms < f->bounce_ms) {
                e->state = IF_SUPPRESSED; /* Chatter: ignore this press until the key is let go. */
            } else if (f->hold_ms == 0) {
                e->state = IF_ACTIVE;
            } else {
                e->state = IF_PENDING;
                e->since_ms = now_ms;
            }
        }
    }
    /* Elements that are no longer down. */
    for (size_t i = 0; i < INPUT_FILTER_MAX; i++) {
        input_filter_entry_t *e = &f->e[i];
        if (e->state == IF_FREE || e->present) {
            continue;
        }
        switch (e->state) {
        case IF_ACTIVE:
            if (f->bounce_ms == 0) {
                e->state = IF_FREE;
            } else {
                e->state = IF_RELEASED;
                e->since_ms = now_ms;
            }
            break;
        case IF_PENDING: /* A tap shorter than the hold time: dropped without a trace, and no lockout either. */
            e->state = IF_FREE;
            break;
        case IF_SUPPRESSED: /* The ghost press ended; the earlier release time still stands. */
            e->state = IF_RELEASED;
            break;
        default:
            break;
        }
    }
    advance(f, now_ms);
}

/** @copydoc input_filter_tick */
void input_filter_tick(input_filter_t *f, uint32_t now_ms) {
    advance(f, now_ms);
}

/** @copydoc input_filter_pending */
bool input_filter_pending(const input_filter_t *f) {
    for (size_t i = 0; i < INPUT_FILTER_MAX; i++) {
        if (f->e[i].state == IF_PENDING && f->e[i].present) {
            return true;
        }
    }
    return false;
}

/** @copydoc input_filter_active */
size_t input_filter_active(const input_filter_t *f, uint8_t *out, size_t out_cap) {
    size_t n = 0;
    for (size_t i = 0; i < INPUT_FILTER_MAX && n < out_cap; i++) {
        if (f->e[i].state == IF_ACTIVE) {
            out[n++] = f->e[i].id;
        }
    }
    return n;
}
