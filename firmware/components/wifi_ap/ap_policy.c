/**
 * @file ap_policy.c
 * @brief AP on/off policy. See ap_policy.h for the rules.
 *
 * Dependencies: libc. Threading: none.
 */
#include "ap_policy.h"

/** Milliseconds per second. */
#define MS_PER_S 1000ULL

/** @copydoc ap_policy_init */
void ap_policy_init(ap_policy_t *p, const ap_policy_config_t *cfg) {
    p->cfg = *cfg;
    p->last_active_ms = 0;
    p->ever_active = false;
    p->forced_off = false;
}

/** @copydoc ap_policy_step */
ap_policy_decision_t ap_policy_step(ap_policy_t *p, const ap_policy_inputs_t *in) {
    ap_policy_decision_t d = {.ap_on = true, .remaining_s = -1};

    /* Host asleep (CLAUDE.md §3.9): the AP is off, even with no bonds. Only a running OTA keeps it up. The clocks keep
     * running, so a boot window that has not expired is still there after the resume. */
    if (in->host_suspended && !in->ota_active) {
        d.ap_on = false;
        d.remaining_s = 0;
        return d;
    }

    /* Explicit "off now" from the user: stays off until the user asks for the AP again. Never while nothing is
     * bonded (the AP is the only way to pair) or while an OTA runs. */
    if (in->force_on || in->bonded_devices == 0 || in->ota_active) {
        p->forced_off = false;
    } else if (in->force_off) {
        p->forced_off = true;
    }
    if (p->forced_off) {
        d.ap_on = false;
        d.remaining_s = 0;
        return d;
    }

    /* Rule 1: no bonds yet (first-run convenience) or OTA running: always on, no countdown.
     * The idle timer restarts when this condition ends, so the AP does not vanish mid-session. */
    if (in->bonded_devices == 0 || in->ota_active) {
        p->last_active_ms = in->now_ms;
        p->ever_active = true;
        return d;
    }

    /* Rules 3 and 4: an open web-app session (or a force request) is "activity". */
    if ((in->clients > 0 && in->ws_alive) || in->force_on) {
        p->last_active_ms = in->now_ms;
        p->ever_active = true;
        return d;
    }

    /* Rule 2 plus the idle tail of rule 3: on until the later of the two deadlines. */
    uint64_t deadline_ms = (uint64_t)p->cfg.boot_window_s * MS_PER_S;
    if (p->ever_active) {
        const uint64_t idle_deadline = p->last_active_ms + (uint64_t)p->cfg.idle_timeout_s * MS_PER_S;
        if (idle_deadline > deadline_ms) {
            deadline_ms = idle_deadline;
        }
    }
    if (in->now_ms < deadline_ms) {
        d.remaining_s = (int32_t)((deadline_ms - in->now_ms + MS_PER_S - 1) / MS_PER_S); /* round up */
    } else {
        d.ap_on = false;
        d.remaining_s = 0;
    }
    return d;
}
