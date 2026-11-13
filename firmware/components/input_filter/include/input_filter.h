/**
 * @file input_filter.h
 * @brief Anti-chatter ("bounce keys") and hold-to-register ("slow keys") filter for keys and mouse buttons (pure C).
 *
 * Responsibilities: sit between a peripheral's normalised state and the USB report so that
 *  - a key or button that is pressed again within `bounce_ms` of its release is ignored (worn switches and shaky hands
 *    produce such double presses), and
 *  - a key or button only counts once it has been held for `hold_ms` (a brush against a key is dropped).
 * Both are off at 0 and the filter is then transparent. The caller decides whether to use it at all (it is off by
 * default in the product).
 *
 * The state is a small table of tracked elements, an element being a key usage, a modifier (0xE0 + bit) or a button
 * number: sparse, so one filter costs about a hundred bytes. A hold time needs a clock tick even when no new report
 * arrives (a BLE keyboard sends nothing while a key stays down): the owner calls input_filter_tick() about every 5 ms
 * while input_filter_pending() is true.
 * Dependencies: libc only. Threading: none, the caller serialises access.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tracked elements per filter (6 keys + 8 modifiers + recently released ones, with margin). */
#define INPUT_FILTER_MAX 24
/** Usage id of the first modifier (Left Control); modifier bit i is element 0xE0 + i. */
#define INPUT_FILTER_MOD_BASE 0xE0

/** Lifecycle of a tracked element. */
typedef enum {
    IF_FREE = 0,   /**< Slot unused. */
    IF_PENDING,    /**< Pressed, waiting for the hold time before it is reported. */
    IF_ACTIVE,     /**< Reported as pressed. */
    IF_SUPPRESSED, /**< Pressed again inside the bounce window: ignored until it is released. */
    IF_RELEASED,   /**< Released recently: a press now is chatter until the bounce window ends. */
} input_filter_state_t;

/** One tracked element. */
typedef struct {
    uint8_t id;        /**< Key usage, modifier id or button number. */
    uint8_t state;     /**< input_filter_state_t. */
    bool present;      /**< Currently down in the input. */
    uint32_t since_ms; /**< Press time (PENDING) or release time (RELEASED). */
} input_filter_entry_t;

/** Filter state. */
typedef struct {
    uint32_t bounce_ms;                       /**< Ignore a re-press this soon after a release (0 = off). */
    uint32_t hold_ms;                         /**< Require this long a press before reporting it (0 = off). */
    input_filter_entry_t e[INPUT_FILTER_MAX]; /**< Tracked elements. */
} input_filter_t;

/**
 * @brief Initialise (or reset) a filter.
 *
 * @param f         Filter.
 * @param bounce_ms Bounce window in ms (0 disables it).
 * @param hold_ms   Hold time in ms (0 disables it).
 */
void input_filter_init(input_filter_t *f, uint32_t bounce_ms, uint32_t hold_ms);

/**
 * @brief Feed the elements currently down.
 *
 * @param f      Filter.
 * @param now_ms Monotonic time in ms (wrap-around safe).
 * @param ids    Elements down now (key usages, 0xE0 + modifier bit, or button numbers); zeros are ignored.
 * @param n      Number of entries in @p ids.
 */
void input_filter_update(input_filter_t *f, uint32_t now_ms, const uint8_t *ids, size_t n);

/**
 * @brief Advance time without new input: promote elements whose hold time has elapsed and forget old releases.
 *
 * @param f      Filter.
 * @param now_ms Monotonic time in ms.
 */
void input_filter_tick(input_filter_t *f, uint32_t now_ms);

/**
 * @brief Whether a tick is needed soon (some element waits for its hold time).
 *
 * @param f Filter.
 * @return true while any element is pending.
 */
bool input_filter_pending(const input_filter_t *f);

/**
 * @brief The elements to report as down.
 *
 * @param f       Filter.
 * @param out     Receives the ids (in table order, which is stable while an element is tracked).
 * @param out_cap Capacity of @p out.
 * @return Number of ids written.
 */
size_t input_filter_active(const input_filter_t *f, uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
