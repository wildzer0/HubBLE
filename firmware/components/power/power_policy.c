/**
 * @file power_policy.c
 * @brief Pure power logic. See power_policy.h.
 *
 * Dependencies: libc. Threading: stateless apart from the caller-owned tracker.
 */
#include "power_policy.h"

/** Floor of the low-power supervision timeout, 10 ms units (4 s): tolerant of a skipped event or two. */
#define LOW_TIMEOUT_FLOOR_10MS 400
/** Safety factor over the worst-case silence. */
#define TIMEOUT_SAFETY 3

/** @copydoc power_tracker_init */
void power_tracker_init(power_tracker_t *t, uint32_t suspend_delay_ms) {
    t->suspend_delay_ms = suspend_delay_ms;
    t->suspended = false;
    t->since_ms = 0;
    t->mode = POWER_ACTIVE;
}

/** @copydoc power_tracker_step */
power_mode_t power_tracker_step(power_tracker_t *t, uint64_t now_ms, bool host_suspended) {
    if (!host_suspended) {
        t->suspended = false;
        t->mode = POWER_ACTIVE; /* Resume is never delayed. */
        return t->mode;
    }
    if (!t->suspended) {
        t->suspended = true;
        t->since_ms = now_ms;
    }
    if (now_ms - t->since_ms >= t->suspend_delay_ms) {
        t->mode = POWER_LOW;
    }
    return t->mode;
}

/** @copydoc power_low_profile */
power_profile_t power_low_profile(uint16_t itvl_units, uint16_t latency) {
    power_profile_t p = {.itvl_units = itvl_units, .latency = latency};
    /* Worst-case silence in ms is (1 + latency) * itvl * 1.25 * 2; in 10 ms units that is (1 + latency) * itvl / 4. */
    uint32_t silence_10ms = ((uint32_t)(1 + latency) * itvl_units + 3) / 4;
    uint32_t t = silence_10ms * TIMEOUT_SAFETY;
    if (t < LOW_TIMEOUT_FLOOR_10MS) {
        t = LOW_TIMEOUT_FLOOR_10MS;
    }
    if (t > POWER_TIMEOUT_MAX_10MS) {
        t = POWER_TIMEOUT_MAX_10MS;
    }
    p.timeout_10ms = (uint16_t)t;
    return p;
}

/** @copydoc power_profile_valid */
bool power_profile_valid(const power_profile_t *p) {
    if (p->itvl_units < POWER_ITVL_MIN_UNITS || p->itvl_units > POWER_ITVL_MAX_UNITS ||
        p->latency > POWER_LATENCY_MAX || p->timeout_10ms < POWER_TIMEOUT_MIN_10MS ||
        p->timeout_10ms > POWER_TIMEOUT_MAX_10MS) {
        return false;
    }
    /* Spec rule: timeout > (1 + latency) * interval * 2. In ms that is timeout_10ms * 10 > (1 + latency) * units * 1.25
     * * 2, i.e. timeout_10ms * 4 > (1 + latency) * units. */
    return (uint32_t)p->timeout_10ms * 4u > (uint32_t)(1 + p->latency) * p->itvl_units;
}
