/**
 * @file settings.h
 * @brief Persistent settings in NVS (Wi-Fi password, AP timings, boot-failure counter, last OTA result).
 *
 * Responsibilities: own the NVS namespace "hubble" and hide key names from the rest of
 * the firmware. Secrets (the Wi-Fi password) are only handed to the Wi-Fi code and are
 * never returned by the API (CLAUDE.md §3.12).
 * Dependencies: nvs_flash, settings_model. Threading: the NVS API is thread-safe;
 * settings_init() must complete before any other call.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length (with NUL) of the stored last-OTA-result text. */
#define SETTINGS_OTA_RESULT_MAX 64

/**
 * @brief Initialise NVS and open the settings namespace.
 *
 * If the NVS partition is full or from another IDF version it is erased and re-created.
 *
 * @return ESP_OK on success, otherwise the NVS error.
 */
esp_err_t settings_init(void);

/**
 * @brief Whether settings_init() succeeded (used by the OTA self-test: "settings readable").
 *
 * @return true if NVS is open and readable.
 */
bool settings_is_ready(void);

/**
 * @brief Get the Wi-Fi AP password (the documented default until the user changes it).
 *
 * @param out Destination buffer.
 * @param cap Capacity of @p out (>= SETTINGS_PASSWORD_MAX_LEN + 1).
 * @return ESP_OK, or ESP_ERR_INVALID_SIZE if @p out is too small.
 */
esp_err_t settings_get_ap_password(char *out, size_t cap);

/**
 * @brief Store a new Wi-Fi AP password after validating it.
 *
 * @param password New password (see settings_password_valid()).
 * @return ESP_OK, ESP_ERR_INVALID_ARG if it fails validation, or the NVS error.
 */
esp_err_t settings_set_ap_password(const char *password);

/**
 * @brief Forget the stored AP password: the default setup password applies again and the setup wizard is shown
 *        (CLAUDE.md §3.10 recovery). Bonds are untouched.
 *
 * @return ESP_OK (also when nothing was stored) or an NVS error.
 */
esp_err_t settings_reset_ap_password(void);

/**
 * @brief Whether the user has set their own Wi-Fi password (setup wizard completed).
 *
 * @return true if a custom password is stored.
 */
bool settings_is_setup_done(void);

/**
 * @brief Setting "Require physical confirmation" for new pairings (default on).
 *
 * @return true if enabled.
 */
bool settings_get_require_confirm(void);

/**
 * @brief Store the "Require physical confirmation" setting.
 *
 * @param enabled New value.
 * @return ESP_OK or an NVS error.
 */
esp_err_t settings_set_require_confirm(bool enabled);

/**
 * @brief Whether station credentials for "check for updates" are stored.
 *
 * @return true if an SSID is stored.
 */
bool settings_has_sta(void);

/**
 * @brief Read the station credentials (only the update engine may call this; they are never sent to the API).
 *
 * @param ssid     Receives the SSID.
 * @param ssid_cap Capacity of @p ssid (at least SETTINGS_SSID_MAX_LEN + 1).
 * @param pass     Receives the password (empty for an open network).
 * @param pass_cap Capacity of @p pass (at least SETTINGS_PASSWORD_MAX_LEN + 1).
 * @return ESP_OK, or ESP_ERR_NOT_FOUND if none are stored.
 */
esp_err_t settings_get_sta(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

/**
 * @brief Store station credentials; an empty SSID clears them.
 *
 * @param ssid     Network name, or "" to forget.
 * @param password Passphrase.
 * @return ESP_OK, ESP_ERR_INVALID_ARG if invalid, or an NVS error.
 */
esp_err_t settings_set_sta(const char *ssid, const char *password);

/**
 * @brief Whether pre-releases are offered by "check for updates".
 *
 * @return true for the pre-release channel, false for stable (default).
 */
bool settings_get_prerelease_channel(void);

/**
 * @brief Select the update channel.
 *
 * @param prerelease true for pre-releases.
 * @return ESP_OK or an NVS error.
 */
esp_err_t settings_set_prerelease_channel(bool prerelease);

/** @brief AP boot window in seconds (stored value or the default). */
uint32_t settings_get_boot_window_s(void);

/** @brief AP idle timeout in seconds (stored value or the default). */
uint32_t settings_get_idle_timeout_s(void);

/** Shortest accepted AP window (boot window / idle timeout), seconds. */
#define SETTINGS_WINDOW_MIN_S 30
/** Longest accepted AP window, seconds (one day). */
#define SETTINGS_WINDOW_MAX_S 86400

/**
 * @brief Whether a value is an acceptable AP window.
 *
 * @param seconds Candidate boot window or idle timeout.
 * @return true if within SETTINGS_WINDOW_MIN_S..SETTINGS_WINDOW_MAX_S.
 */
bool settings_window_valid(uint32_t seconds);

/**
 * @brief Store the AP boot window. Applied at the next boot (the AP manager reads it at start).
 *
 * @param seconds New value.
 * @return ESP_OK, ESP_ERR_INVALID_ARG if out of range, or an NVS error.
 */
esp_err_t settings_set_boot_window_s(uint32_t seconds);

/**
 * @brief Store the AP idle timeout. Applied at the next boot.
 *
 * @param seconds New value.
 * @return ESP_OK, ESP_ERR_INVALID_ARG if out of range, or an NVS error.
 */
esp_err_t settings_set_idle_timeout_s(uint32_t seconds);

/**
 * @brief Increment the consecutive-boot-failure counter and return the new value.
 *
 * @return Counter including the current boot.
 */
uint32_t settings_boot_failures_increment(void);

/** @brief Current boot-failure counter. */
uint32_t settings_boot_failures(void);

/** @brief Reset the boot-failure counter to zero (self-test passed or new image installed). */
void settings_boot_failures_reset(void);

/**
 * @brief Persist a short text describing the last OTA attempt (shown on the Update page).
 *
 * @param text NUL-terminated text, truncated to SETTINGS_OTA_RESULT_MAX - 1.
 */
void settings_set_ota_result(const char *text);

/**
 * @brief Read the last OTA result text.
 *
 * @param out Destination buffer.
 * @param cap Capacity of @p out. Empty string if none stored.
 */
void settings_get_ota_result(char *out, size_t cap);

/** LED brightness limits (percent) and default. 0 disables the status LED. */
#define SETTINGS_LED_BRIGHTNESS_MAX     100
#define SETTINGS_LED_BRIGHTNESS_DEFAULT 25
/** Ctrl+Alt hold time limits and default, ms (CLAUDE.md §3.5: default 3 s). */
#define SETTINGS_SHIFT_HOLD_MIN_MS     1000
#define SETTINGS_SHIFT_HOLD_MAX_MS     10000
#define SETTINGS_SHIFT_HOLD_DEFAULT_MS 3000

/**
 * @brief Whether joining the Wi-Fi should pop up the web app (captive portal). Default false: the dongle then answers
 * the operating systems' connectivity probes as if the network had internet, so nothing pops up and the phone does not
 * drop the network; the page is opened from the QR code, `hubble.local` or `192.168.4.1`. Until the setup wizard is
 * done the popup is always on.
 */
bool settings_get_portal_auto(void);
/** @brief Store the captive-portal popup flag. @param enabled New value. @return ESP_OK or an NVS error. */
esp_err_t settings_set_portal_auto(bool enabled);

/** @brief Whether a peripheral event may wake the suspended host (default true, CLAUDE.md §3.9). */
bool settings_get_remote_wakeup(void);
/** @brief Store the remote-wakeup setting. @param enabled New value. @return ESP_OK or an NVS error. */
esp_err_t settings_set_remote_wakeup(bool enabled);

/** @brief LED brightness in percent (0 = LED off). */
uint8_t settings_get_led_brightness(void);
/** @brief Store the LED brightness. @param percent 0..100. @return ESP_OK, ESP_ERR_INVALID_ARG or an NVS error. */
esp_err_t settings_set_led_brightness(uint32_t percent);

/** @brief Whether the LED shows dim green while a device is connected (default false). */
bool settings_get_status_led(void);
/** @brief Store the status-LED setting. @param enabled New value. @return ESP_OK or an NVS error. */
esp_err_t settings_set_status_led(bool enabled);

/** @brief Hold time of the Ctrl+Alt gesture in ms. Applied at the next boot. */
uint32_t settings_get_shift_hold_ms(void);
/** @brief Store the gesture hold time. @param ms 1000..10000. @return ESP_OK, ESP_ERR_INVALID_ARG or an NVS error. */
esp_err_t settings_set_shift_hold_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
