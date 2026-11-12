/**
 * @file power_policy.h
 * @brief Pure power logic: follow the host's USB suspend state (CLAUDE.md §3.9).
 *
 * Rules:
 *  - when the host suspends the bus the dongle enters low-power mode, but only after the bus stayed suspended for a
 *    short debounce time (hosts sometimes suspend for a moment between transfers);
 *  - when the host resumes, the active mode returns IMMEDIATELY (the 7.5 ms interval must be back before the user's
 *    first key press matters);
 *  - the low-power BLE profile uses a long connection interval and a high peripheral latency, with a supervision
 *    timeout that is always valid for them.
 *
 * Dependencies: libc only (host-testable). Threading: caller serialises access (single power task).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power mode of the dongle. */
typedef enum {
    POWER_ACTIVE = 0, /**< Host awake (or unknown): full speed. */
    POWER_LOW         /**< Host suspended: peripherals save battery, LED and AP are off. */
} power_mode_t;

/** BLE connection parameters (units as in the Bluetooth specification). */
typedef struct {
    uint16_t itvl_units;   /**< Connection interval in 1.25 ms units. */
    uint16_t latency;      /**< Peripheral latency (connection events the peripheral may skip). */
    uint16_t timeout_10ms; /**< Supervision timeout in 10 ms units. */
} power_profile_t;

/** Shortest supervision timeout the specification allows, 10 ms units. */
#define POWER_TIMEOUT_MIN_10MS 10
/** Longest supervision timeout the specification allows, 10 ms units. */
#define POWER_TIMEOUT_MAX_10MS 3200
/** Largest peripheral latency the specification allows. */
#define POWER_LATENCY_MAX 499
/** Smallest and largest connection interval the specification allows, 1.25 ms units. */
#define POWER_ITVL_MIN_UNITS 6
#define POWER_ITVL_MAX_UNITS 3200

/** Debounce tracker. Initialise with power_tracker_init(). */
typedef struct {
    uint32_t suspend_delay_ms; /**< The bus must stay suspended this long before low-power mode starts. */
    bool suspended;            /**< Last observed bus state. */
    uint64_t since_ms;         /**< When the bus became suspended. */
    power_mode_t mode;         /**< Current mode. */
} power_tracker_t;

/**
 * @brief Initialise the tracker in active mode.
 *
 * @param t                Tracker.
 * @param suspend_delay_ms Debounce time before low power starts (0 = immediately).
 */
void power_tracker_init(power_tracker_t *t, uint32_t suspend_delay_ms);

/**
 * @brief Feed the current bus state and get the mode.
 *
 * @param t              Tracker (updated).
 * @param now_ms         Monotonic time in milliseconds.
 * @param host_suspended true while the USB bus is suspended.
 * @return The mode to apply now.
 */
power_mode_t power_tracker_step(power_tracker_t *t, uint64_t now_ms, bool host_suspended);

/**
 * @brief Build a low-power profile with a supervision timeout that is valid for it.
 *
 * The timeout is at least 3 times the worst-case silence ((1 + latency) * interval * 2), but never below 4 s and
 * clamped to the specification maximum.
 *
 * @param itvl_units Connection interval, 1.25 ms units.
 * @param latency    Peripheral latency.
 * @return The profile.
 */
power_profile_t power_low_profile(uint16_t itvl_units, uint16_t latency);

/**
 * @brief Check a profile against the Bluetooth rules (ranges, and supervision timeout > (1 + latency) * interval * 2).
 *
 * @param p Profile.
 * @return true if a controller will accept it.
 */
bool power_profile_valid(const power_profile_t *p);

#ifdef __cplusplus
}
#endif
