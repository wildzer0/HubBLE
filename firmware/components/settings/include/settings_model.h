/**
 * @file settings_model.h
 * @brief Pure settings rules: defaults and validation (host-testable).
 *
 * Dependencies: libc only. Threading: stateless.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Documented default Wi-Fi password used until the setup wizard changes it (CLAUDE.md §3.5). */
#define SETTINGS_DEFAULT_AP_PASSWORD "hubble-setup"
/** Minimum length of a user-chosen Wi-Fi password (CLAUDE.md §3.5). */
#define SETTINGS_PASSWORD_MIN_LEN 10
/** Maximum length of a WPA2 passphrase accepted by ESP-IDF. */
#define SETTINGS_PASSWORD_MAX_LEN 63
/** Default time the AP stays on after power-up, in seconds (5 min). */
#define SETTINGS_DEFAULT_BOOT_WINDOW_S 300U
/** Default idle time after the last client left before the AP switches off, in seconds (3 min). */
#define SETTINGS_DEFAULT_IDLE_TIMEOUT_S 180U

/**
 * @brief Validate a user-chosen Wi-Fi password.
 *
 * Rules: 10..63 printable ASCII characters (space allowed), and it must differ
 * from the documented default password.
 *
 * @param password NUL-terminated candidate.
 * @return true if acceptable.
 */
bool settings_password_valid(const char *password);

#ifdef __cplusplus
}
#endif
