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
 * @brief Whether the user has set their own Wi-Fi password (setup wizard completed).
 *
 * @return true if a custom password is stored.
 */
bool settings_is_setup_done(void);

/** @brief AP boot window in seconds (stored value or the default). */
uint32_t settings_get_boot_window_s(void);

/** @brief AP idle timeout in seconds (stored value or the default). */
uint32_t settings_get_idle_timeout_s(void);

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

#ifdef __cplusplus
}
#endif
