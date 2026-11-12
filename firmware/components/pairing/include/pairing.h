/**
 * @file pairing.h
 * @brief Secure pairing state machine and challenges (pure C, host-testable; CLAUDE.md §3.4).
 *
 * States: IDLE -> SCANNING -> CONNECTING -> PAIRING -> QUARANTINE -> CONFIRM -> BONDED | FAILED.
 *  - A device that authenticated with a passkey (keyboard, LE Secure Connections) is 🟢 and skips QUARANTINE.
 *  - A Just Works device is 🟡: it stays in QUARANTINE (connected, nothing forwarded to the host) until the user
 *    proves *which* device it is: a click sequence (mouse, 30 s) or a typed code (keyboard).
 *  - Legacy pairing (no LE Secure Connections) is rejected unless the user enabled "allow legacy" for this
 *    pairing; it is then 🔴.
 *  - With "require physical confirmation" the last step is CONFIRM: Ctrl + Alt held 3 s on an ALREADY paired
 *    keyboard (skipped when there is none).
 *  - Rate limiting: 3 consecutive failures lock pairing for 60 s.
 * What the quarantine does NOT protect against: an active radio man-in-the-middle during the pairing window.
 * It only verifies which device is being paired.
 * Dependencies: libc only. Threading: no locking; the owner (ble_central) serialises access.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pairing flow state. */
typedef enum {
    PAIR_IDLE = 0,   /**< Nothing running. */
    PAIR_SCANNING,   /**< Listing nearby HID advertisers. */
    PAIR_CONNECTING, /**< Connecting to the chosen device. */
    PAIR_PAIRING,    /**< Link up: SMP pairing (passkey shown here) and HID discovery. */
    PAIR_QUARANTINE, /**< Just Works device connected but muted until the challenge is solved. */
    PAIR_CONFIRM,    /**< Waiting for the physical confirmation on an already paired keyboard. */
    PAIR_BONDED,     /**< Success: the device is registered. */
    PAIR_FAILED,     /**< Failure; see pairing_t::fail. */
} pair_state_t;

/** Security level of a bond (badge). */
typedef enum {
    SEC_UNKNOWN = 0, /**< Unknown (device bonded by an older build). */
    SEC_GREEN,       /**< 🟢 LE Secure Connections + authenticated (passkey): MITM protected. */
    SEC_YELLOW,      /**< 🟡 LE Secure Connections Just Works + verified challenge: no MITM protection. */
    SEC_RED,         /**< 🔴 Legacy pairing allowed by the user. */
} sec_level_t;

/** Why a pairing failed. */
typedef enum {
    FAIL_NONE = 0,  /**< No failure. */
    FAIL_TIMEOUT,   /**< A phase timed out. */
    FAIL_CANCELLED, /**< The user cancelled (not counted for the lock). */
    FAIL_LEGACY,    /**< Legacy pairing refused by policy. */
    FAIL_SECURITY,  /**< Encryption/SMP failed or the link dropped. */
    FAIL_NO_HID,    /**< No usable HID service. */
    FAIL_CHALLENGE, /**< Too many wrong challenge inputs. */
} pair_fail_t;

/** Challenge kinds. */
typedef enum {
    CHALLENGE_NONE = 0, /**< No challenge. */
    CHALLENGE_MOUSE,    /**< Click sequence. */
    CHALLENGE_KEYBOARD, /**< Typed code. */
} challenge_kind_t;

/** Length of a mouse click sequence (CLAUDE.md §3.4: 5-6 clicks). */
#define CHALLENGE_MOUSE_LEN 6
/** Length of the keyboard code. */
#define CHALLENGE_CODE_LEN 6
/** Wrong inputs tolerated before the challenge fails. */
#define CHALLENGE_MAX_MISTAKES 3
/** Phase durations in ms. */
#define PAIR_PAIRING_TIMEOUT_MS   60000u
#define PAIR_CHALLENGE_TIMEOUT_MS 30000u
#define PAIR_CONFIRM_TIMEOUT_MS   30000u
/** Lock after this many consecutive failures, and for how long (CLAUDE.md §3.12). */
#define PAIR_MAX_FAILURES 3
#define PAIR_LOCK_MS      60000u
/** Number of digits of the passkey. */
#define PAIR_PASSKEY_DIGITS 6

/** A challenge in progress. */
typedef struct {
    challenge_kind_t kind;             /**< Kind. */
    char text[CHALLENGE_CODE_LEN + 1]; /**< Mouse: sequence of 'L','R','M'. Keyboard: the code. NUL-terminated. */
    uint8_t len;                       /**< Number of expected inputs. */
    uint8_t pos;                       /**< Inputs matched so far. */
    uint8_t mistakes;                  /**< Wrong inputs so far. */
    uint8_t prev_buttons;              /**< Last mouse button state (edge detection). */
    uint8_t prev_keys[6];              /**< Last key list (edge detection). */
} challenge_t;

/** The state machine. */
typedef struct {
    pair_state_t state;        /**< Current state. */
    pair_fail_t fail;          /**< Reason of the last failure (valid in FAILED). */
    sec_level_t level;         /**< Level reached so far (final in BONDED). */
    bool authenticated;        /**< The SMP pairing was authenticated (passkey). */
    bool allow_legacy;         /**< Per-pairing override. */
    bool require_confirm;      /**< Setting "Require physical confirmation". */
    bool need_confirm;         /**< This pairing needs the confirmation (setting && another keyboard exists). */
    uint32_t phase_start_ms;   /**< Start of the current timed phase. */
    uint32_t phase_timeout_ms; /**< Its duration (0 = untimed). */
    bool has_passkey;          /**< A passkey is being displayed. */
    uint32_t passkey;          /**< The passkey (valid while has_passkey). */
    challenge_t challenge;     /**< The challenge (valid in QUARANTINE). */
    uint8_t failures;          /**< Consecutive failures. */
    bool locked;               /**< Pairing is locked after repeated failures. */
    uint32_t locked_until_ms;  /**< End of the lock. */
} pairing_t;

/** Result of a start request. */
typedef enum {
    PAIR_START_OK = 0, /**< Accepted. */
    PAIR_START_BUSY,   /**< A pairing is already running. */
    PAIR_START_LOCKED, /**< Locked after repeated failures. */
} pair_start_t;

/** Random number source (injected so tests are deterministic and firmware uses the hardware RNG). */
typedef uint32_t (*pair_rng_t)(void);

/**
 * @brief Initialise the machine.
 *
 * @param p               Machine.
 * @param require_confirm Setting "Require physical confirmation".
 */
void pairing_init(pairing_t *p, bool require_confirm);

/**
 * @brief Whether a pairing is in progress (any state from SCANNING to CONFIRM).
 *
 * @param p Machine.
 * @return true if active.
 */
bool pairing_active(const pairing_t *p);

/**
 * @brief Seconds of lock left (0 if not locked).
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @return Seconds remaining.
 */
uint32_t pairing_lock_remaining_s(const pairing_t *p, uint32_t now_ms);

/**
 * @brief Milliseconds left in the current timed phase (0 if untimed or expired).
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @return Remaining ms.
 */
uint32_t pairing_remaining_ms(const pairing_t *p, uint32_t now_ms);

/**
 * @brief Start a scan (IDLE/BONDED/FAILED -> SCANNING).
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @return PAIR_START_OK, _BUSY or _LOCKED.
 */
pair_start_t pairing_begin_scan(pairing_t *p, uint32_t now_ms);

/**
 * @brief The scan ended without a selection (SCANNING -> IDLE).
 *
 * @param p Machine.
 */
void pairing_scan_done(pairing_t *p);

/**
 * @brief Start pairing with a chosen device (-> CONNECTING).
 *
 * @param p                  Machine.
 * @param now_ms             Current time.
 * @param allow_legacy       Per-pairing legacy override.
 * @param have_other_keyboard An already paired keyboard exists (so physical confirmation is possible).
 * @return PAIR_START_OK, _BUSY or _LOCKED.
 */
pair_start_t pairing_begin_pair(pairing_t *p, uint32_t now_ms, bool allow_legacy, bool have_other_keyboard);

/**
 * @brief The link is up (CONNECTING -> PAIRING).
 *
 * @param p      Machine.
 * @param now_ms Current time.
 */
void pairing_on_connected(pairing_t *p, uint32_t now_ms);

/**
 * @brief Generate a random 6-digit passkey and start displaying it (PAIRING only).
 *
 * @param p   Machine.
 * @param rng Random source.
 * @return The passkey (000000..999999), or 0 if not in PAIRING.
 */
uint32_t pairing_make_passkey(pairing_t *p, pair_rng_t rng);

/**
 * @brief The link got encrypted: apply the legacy policy and compute the level (PAIRING only).
 *
 * @param p             Machine.
 * @param now_ms        Current time.
 * @param authenticated The pairing was authenticated (MITM protected).
 * @param secure_conn   LE Secure Connections were used.
 * @return false if the pairing was rejected by policy (state is now FAILED/FAIL_LEGACY).
 */
bool pairing_on_secured(pairing_t *p, uint32_t now_ms, bool authenticated, bool secure_conn);

/**
 * @brief HID discovery finished: move to QUARANTINE (challenge) or on to CONFIRM/BONDED.
 *
 * @param p           Machine.
 * @param now_ms      Current time.
 * @param is_keyboard The device has keyboard input.
 * @param rng         Random source for the challenge.
 */
void pairing_on_hid_ready(pairing_t *p, uint32_t now_ms, bool is_keyboard, pair_rng_t rng);

/**
 * @brief Feed the mouse button state of the quarantined device.
 *
 * @param p       Machine.
 * @param now_ms  Current time.
 * @param buttons hid_mouse_button_t bits currently pressed.
 */
void pairing_input_buttons(pairing_t *p, uint32_t now_ms, uint8_t buttons);

/**
 * @brief Feed the pressed keys of the quarantined keyboard.
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @param keys   Pressed key usages (6 entries, 0 = empty).
 */
void pairing_input_keys(pairing_t *p, uint32_t now_ms, const uint8_t keys[6]);

/**
 * @brief Physical confirmation received (CONFIRM -> BONDED). Ignored in other states.
 *
 * @param p      Machine.
 * @param now_ms Current time.
 */
void pairing_confirm(pairing_t *p, uint32_t now_ms);

/**
 * @brief Report a failure from the radio side (link dropped, encryption failed, no HID service...).
 *
 * @param p      Machine.
 * @param now_ms Current time.
 * @param reason Reason.
 */
void pairing_fail(pairing_t *p, uint32_t now_ms, pair_fail_t reason);

/**
 * @brief The user cancelled (active -> FAILED/FAIL_CANCELLED, not counted towards the lock).
 *
 * @param p Machine.
 */
void pairing_cancel(pairing_t *p);

/**
 * @brief Periodic tick: phase timeouts and lock expiry. Call about once a second.
 *
 * @param p      Machine.
 * @param now_ms Current time.
 */
void pairing_tick(pairing_t *p, uint32_t now_ms);

/**
 * @brief Map a keyboard usage to the challenge alphabet.
 *
 * @param usage Key usage (page 0x07).
 * @return 'A'..'Z' or '0'..'9', or 0 if the key is not alphanumeric.
 */
char challenge_usage_to_char(uint8_t usage);

#ifdef __cplusplus
}
#endif
