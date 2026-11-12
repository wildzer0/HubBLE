/**
 * @file combo.h
 * @brief Keyboard hold-combo detector (pure C): "both Shifts" and "both Shifts + Esc".
 *
 * Responsibilities (CLAUDE.md §3.5, §3.10): detect, on the MERGED keyboard state of all paired keyboards,
 *  - AP_ON: both Shift keys held for the configured time (default 3 s) and no other key;
 *  - RECOVERY: both Shift keys + Esc held for 10 s and no other key (Wi-Fi password reset).
 * Each combo fires once per hold and re-arms only after the keys are released. The key events themselves
 * are still forwarded to the host: they are harmless (CLAUDE.md §3.5).
 * Dependencies: libc only. Threading: no locking; call from one task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Modifier bits of the HID modifier byte used by the combos. */
#define COMBO_MOD_LSHIFT 0x02
#define COMBO_MOD_RSHIFT 0x20
/** Escape key usage (page 0x07). */
#define COMBO_KEY_ESC 0x29
/** Default hold times in ms (CLAUDE.md §3.5 and §3.10). */
#define COMBO_DEFAULT_AP_HOLD_MS 3000
#define COMBO_RECOVERY_HOLD_MS   10000

/** What combo_update() detected. */
typedef enum {
    COMBO_NONE = 0, /**< Nothing (new) happened. */
    COMBO_AP_ON,    /**< Both Shifts held long enough: turn the Wi-Fi AP on. */
    COMBO_RECOVERY, /**< Both Shifts + Esc held long enough: reset the Wi-Fi password. */
} combo_event_t;

/** Detector state. */
typedef struct {
    uint32_t ap_hold_ms; /**< Hold time for AP_ON (configurable). */
    uint32_t since_ms;   /**< Time the current candidate state started. */
    uint8_t phase;       /**< 0 = idle, 1 = both Shifts only, 2 = both Shifts + Esc only. */
    bool ap_fired;       /**< AP_ON already fired for this hold. */
    bool recovery_fired; /**< RECOVERY already fired for this hold. */
} combo_t;

/**
 * @brief Initialise the detector.
 *
 * @param c          Detector.
 * @param ap_hold_ms Hold time for AP_ON in ms (0 selects COMBO_DEFAULT_AP_HOLD_MS).
 */
void combo_init(combo_t *c, uint32_t ap_hold_ms);

/**
 * @brief Feed the current merged keyboard state.
 *
 * Call whenever the keyboard state changes and also periodically (e.g. every 100 ms) so that holding still
 * is noticed. Wrap-around of the 32-bit millisecond clock is handled.
 *
 * @param c         Detector.
 * @param now_ms    Monotonic time in ms.
 * @param modifiers Merged modifier byte.
 * @param keys      Merged key array (6 entries, 0 = empty).
 * @param nkeys     Number of entries in @p keys (normally 6).
 * @return The combo that just completed, or COMBO_NONE.
 */
combo_event_t combo_update(combo_t *c, uint32_t now_ms, uint8_t modifiers, const uint8_t *keys, unsigned nkeys);

#ifdef __cplusplus
}
#endif
