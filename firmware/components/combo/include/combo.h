/**
 * @file combo.h
 * @brief Keyboard hold-combo detector (pure C): "Ctrl + Alt" and "Ctrl + Alt + Esc".
 *
 * Responsibilities (CLAUDE.md §3.5, §3.10): detect, on the MERGED keyboard state of all paired keyboards,
 *  - AP_ON: a Ctrl key and an Alt key (either side) held for the configured time (default 3 s) and nothing else;
 *  - RECOVERY: Ctrl + Alt + Esc held for 10 s and nothing else (Wi-Fi password reset).
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

/** Modifier bits of the HID modifier byte used by the combos (either side counts). */
#define COMBO_MOD_LCTRL  0x01
#define COMBO_MOD_LSHIFT 0x02
#define COMBO_MOD_LALT   0x04
#define COMBO_MOD_RCTRL  0x10
#define COMBO_MOD_RALT   0x40
/** Escape key usage (page 0x07). */
#define COMBO_KEY_ESC 0x29
/** Default hold times in ms (CLAUDE.md §3.5 and §3.10). */
#define COMBO_DEFAULT_AP_HOLD_MS 3000
#define COMBO_RECOVERY_HOLD_MS   10000

/** What combo_update() detected. */
typedef enum {
    COMBO_NONE = 0, /**< Nothing (new) happened. */
    COMBO_AP_ON,    /**< Ctrl + Alt held long enough: turn the Wi-Fi AP on. */
    COMBO_RECOVERY, /**< Ctrl + Alt + Esc held long enough: reset the Wi-Fi password. */
} combo_event_t;

/** Detector state. */
typedef struct {
    uint32_t ap_hold_ms; /**< Hold time for AP_ON (configurable). */
    uint32_t since_ms;   /**< Time the current candidate state started. */
    uint8_t phase;       /**< 0 = idle, 1 = Ctrl + Alt only, 2 = Ctrl + Alt + Esc only. */
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

/* ---- Gestures that need no keyboard ------------------------------------------------------------------ */

/** Mouse buttons of the pointer gesture (bits of the merged buttons byte). */
#define COMBO_BTN_LEFT  0x01
#define COMBO_BTN_RIGHT 0x02
/** A press of the board button counts as "short" when it lasts from this... */
#define COMBO_BUTTON_MIN_MS 30
/** ... up to this: longer presses are ignored (holding BOOT while plugging in selects the ROM downloader). */
#define COMBO_BUTTON_MAX_MS 2000

/** Detector of "left and right mouse button held together", for setups without a keyboard. */
typedef struct {
    uint32_t hold_ms;  /**< Hold time (the same setting as the keyboard gesture). */
    uint32_t since_ms; /**< When the buttons went down together. */
    bool active;       /**< Exactly the two buttons are down. */
    bool fired;        /**< Already reported for this hold. */
} combo_mouse_t;

/**
 * @brief Initialise the pointer-gesture detector.
 *
 * @param c       Detector.
 * @param hold_ms Hold time in ms (0 selects COMBO_DEFAULT_AP_HOLD_MS).
 */
void combo_mouse_init(combo_mouse_t *c, uint32_t hold_ms);

/**
 * @brief Feed the merged mouse buttons.
 *
 * @param c       Detector.
 * @param now_ms  Monotonic time in ms (wrap-around safe).
 * @param buttons Merged buttons byte of all paired pointing devices.
 * @return true once per hold, when exactly Left + Right have been held for the hold time and nothing else is down.
 */
bool combo_mouse_update(combo_mouse_t *c, uint32_t now_ms, uint8_t buttons);

/** Detector of a short press of the board's button (BOOT). */
typedef struct {
    uint32_t since_ms; /**< When the button went down. */
    bool down;         /**< The button is currently down. */
} combo_button_t;

/**
 * @brief Initialise the button detector.
 *
 * @param c Detector.
 */
void combo_button_init(combo_button_t *c);

/**
 * @brief Feed the button level.
 *
 * @param c       Detector.
 * @param now_ms  Monotonic time in ms (wrap-around safe).
 * @param pressed true while the button is pressed.
 * @return true on the release of a press that lasted COMBO_BUTTON_MIN_MS..COMBO_BUTTON_MAX_MS.
 */
bool combo_button_update(combo_button_t *c, uint32_t now_ms, bool pressed);

#ifdef __cplusplus
}
#endif
