/**
 * @file lat_stats.c
 * @brief Latency statistics (see lat_stats.h).
 *
 * Dependencies: libc only. Threading: none.
 */
#include "lat_stats.h"

#include <string.h>

/** @copydoc lat_stats_reset */
void lat_stats_reset(lat_stats_t *s) {
    memset(s, 0, sizeof(*s));
}

/** @copydoc lat_stats_record */
void lat_stats_record(lat_stats_t *s, uint32_t us) {
    if (s->count == 0 || us < s->min_us) {
        s->min_us = us;
    }
    if (us > s->max_us) {
        s->max_us = us;
    }
    s->sum_us += us;
    s->count++;
    uint32_t b = us / LAT_BUCKET_US;
    s->hist[b < LAT_BUCKETS ? b : LAT_BUCKETS]++;
}

/** @copydoc lat_stats_summary */
void lat_stats_summary(const lat_stats_t *s, lat_summary_t *out) {
    memset(out, 0, sizeof(*out));
    if (s->count == 0) {
        return;
    }
    out->count = s->count;
    out->min_us = s->min_us;
    out->max_us = s->max_us;
    out->avg_us = (uint32_t)(s->sum_us / s->count);
    /* p95: smallest bucket whose cumulative count reaches 95 % of the samples (rounded up). */
    uint32_t target = (uint32_t)(((uint64_t)s->count * 95 + 99) / 100);
    uint32_t acc = 0;
    for (uint32_t b = 0; b <= LAT_BUCKETS; b++) {
        acc += s->hist[b];
        if (acc >= target) {
            uint32_t edge = (b + 1) * LAT_BUCKET_US;
            /* The upper edge of a bucket can exceed the real maximum; never report more than that. */
            out->p95_us = edge > s->max_us ? s->max_us : edge;
            return;
        }
    }
    out->p95_us = s->max_us;
}
