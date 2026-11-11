/**
 * @file ap_policy.h
 * @brief Pure decision logic: should the Wi-Fi access point be on? (host-testable)
 *
 * Rules (CLAUDE.md §3.5): the AP is on
 *  1. always while no peripheral is bonded, and while an OTA is running;
 *  2. during the boot window after power-up;
 *  3. while at least one Wi-Fi client is connected AND the web app is open
 *     (WebSocket heartbeat alive); after the last such client leaves it stays on
 *     for the idle timeout;
 *  4. when a "force on" request arrives (double-Shift combo, M3); this counts as
 *     activity, so the idle timeout then applies.
 * Dependencies: libc only. Threading: caller serialises access (single manager task).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Timing configuration. */
typedef struct {
    uint32_t boot_window_s;  /**< AP stays on this long after power-up. */
    uint32_t idle_timeout_s; /**< AP stays on this long after the last active client. */
} ap_policy_config_t;

/** Snapshot of the world, sampled by the caller on every step. */
typedef struct {
    uint64_t now_ms;         /**< Milliseconds since boot. */
    uint32_t bonded_devices; /**< Number of bonded BLE peripherals. */
    uint32_t clients;        /**< Wi-Fi stations currently associated to the AP. */
    bool ws_alive;           /**< A web app WebSocket heartbeat was seen recently. */
    bool ota_active;         /**< An OTA upload/install is in progress. */
    bool force_on;           /**< One-shot request to switch the AP on (e.g. Shift combo). */
} ap_policy_inputs_t;

/** Result of one evaluation. */
typedef struct {
    bool ap_on;          /**< Desired AP state. */
    int32_t remaining_s; /**< Seconds until the AP switches off, or -1 if no countdown is running. */
} ap_policy_decision_t;

/** Policy state. Initialise with ap_policy_init(). */
typedef struct {
    ap_policy_config_t cfg;  /**< Timing configuration. */
    uint64_t last_active_ms; /**< Last time a client+web-app session (or force request) was seen. */
    bool ever_active;        /**< true once last_active_ms is meaningful. */
} ap_policy_t;

/**
 * @brief Initialise the policy.
 *
 * @param p   Policy state.
 * @param cfg Timing configuration (copied).
 */
void ap_policy_init(ap_policy_t *p, const ap_policy_config_t *cfg);

/**
 * @brief Evaluate the policy for the current inputs.
 *
 * @param p  Policy state (updated).
 * @param in Current inputs.
 * @return Desired AP state and countdown.
 */
ap_policy_decision_t ap_policy_step(ap_policy_t *p, const ap_policy_inputs_t *in);

#ifdef __cplusplus
}
#endif
