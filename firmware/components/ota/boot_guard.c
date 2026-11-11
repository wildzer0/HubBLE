/**
 * @file boot_guard.c
 * @brief Boot-failure counter policy. See boot_guard.h.
 *
 * Dependencies: none. Threading: stateless.
 */
#include "boot_guard.h"

/** @copydoc boot_guard_decide */
boot_guard_action_t boot_guard_decide(uint32_t failures, bool rollback_possible) {
    if (failures < BOOT_GUARD_MAX_FAILURES) {
        return BOOT_GUARD_CONTINUE;
    }
    return rollback_possible ? BOOT_GUARD_ROLLBACK : BOOT_GUARD_STUCK;
}
