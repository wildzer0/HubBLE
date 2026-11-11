/**
 * @file boot_guard.h
 * @brief Decision logic for the boot-failure counter (pure C, host-testable).
 *
 * The counter is incremented at every boot and cleared once the self-test passes.
 * A crash loop therefore leaves it growing; at the limit a pending image is
 * rolled back (CLAUDE.md §3.8). Dependencies: none. Threading: stateless.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of consecutive failed boots that triggers a rollback. */
#define BOOT_GUARD_MAX_FAILURES 3U

/** What the boot code should do. */
typedef enum {
    BOOT_GUARD_CONTINUE = 0, /**< Boot normally. */
    BOOT_GUARD_ROLLBACK,     /**< Mark this image invalid and reboot into the previous one. */
    BOOT_GUARD_STUCK,        /**< Too many failures but no previous image to go back to: keep going, report. */
} boot_guard_action_t;

/**
 * @brief Decide what to do given the failure count including the current boot.
 *
 * @param failures          Consecutive boots that did not reach a passed self-test.
 * @param rollback_possible true if a previous valid image exists.
 * @return The action to take.
 */
boot_guard_action_t boot_guard_decide(uint32_t failures, bool rollback_possible);

#ifdef __cplusplus
}
#endif
