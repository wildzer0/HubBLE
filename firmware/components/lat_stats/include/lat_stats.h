/**
 * @file lat_stats.h
 * @brief Latency statistics (pure C): min / average / p95 / max from a fixed histogram.
 *
 * Responsibilities (CLAUDE.md §3.11): record the time between a BLE notification arriving and the USB report
 * being queued, per device, with no allocation. Percentiles come from a histogram with 250 us buckets up to
 * 16 ms plus an overflow bucket, which is accurate enough for the 15 ms target.
 * Dependencies: libc only. Threading: no locking; the owner serialises record/snapshot.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Histogram bucket width in microseconds. */
#define LAT_BUCKET_US 250
/** Number of regular buckets (covers 0..16 ms); one more catches everything above. */
#define LAT_BUCKETS 64

/** Accumulator. */
typedef struct {
    uint32_t count;                 /**< Samples recorded. */
    uint32_t min_us;                /**< Smallest sample (valid when count > 0). */
    uint32_t max_us;                /**< Largest sample. */
    uint64_t sum_us;                /**< Sum of samples, for the average. */
    uint32_t hist[LAT_BUCKETS + 1]; /**< Histogram; the last entry is the overflow bucket. */
} lat_stats_t;

/** Summary of a lat_stats_t. */
typedef struct {
    uint32_t count;  /**< Samples. */
    uint32_t min_us; /**< Minimum. */
    uint32_t avg_us; /**< Average. */
    uint32_t p95_us; /**< 95th percentile (upper edge of its bucket). */
    uint32_t max_us; /**< Maximum. */
} lat_summary_t;

/**
 * @brief Reset an accumulator.
 *
 * @param s Accumulator.
 */
void lat_stats_reset(lat_stats_t *s);

/**
 * @brief Record one sample.
 *
 * @param s  Accumulator.
 * @param us Latency in microseconds.
 */
void lat_stats_record(lat_stats_t *s, uint32_t us);

/**
 * @brief Summarise an accumulator.
 *
 * @param s   Accumulator.
 * @param out Receives the summary (all zero when no sample was recorded).
 */
void lat_stats_summary(const lat_stats_t *s, lat_summary_t *out);

#ifdef __cplusplus
}
#endif
