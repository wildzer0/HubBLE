/**
 * @file wifi_ap.h
 * @brief Wi-Fi SoftAP manager: SSID/password, on/off policy, client counting.
 *
 * Responsibilities: bring up the netif/event loop, configure the WPA2 SoftAP `HubBLE-XXXX`,
 * and run a manager task that applies ap_policy once per second (CLAUDE.md §3.5).
 * Dependencies: esp_wifi, esp_netif, settings, ap_policy.
 * Threading: wifi_ap_init() once from app_main; the other functions are safe from any task
 * (they only set atomics or read a mutex-protected status copy).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot of the AP state. */
typedef struct {
    bool on;           /**< Radio/AP currently running. */
    int32_t timeout_s; /**< Seconds until switch-off, or -1 if no countdown. */
    uint32_t clients;  /**< Associated stations. */
    char ssid[33];     /**< Current SSID. */
} wifi_ap_status_t;

/**
 * @brief Initialise networking and start the AP manager task.
 *
 * @return ESP_OK or the first error from netif/wifi initialisation.
 */
esp_err_t wifi_ap_init(void);

/**
 * @brief Get a snapshot of the AP status.
 *
 * @param out Receives the status.
 */
void wifi_ap_get_status(wifi_ap_status_t *out);

/** @brief Tell the manager a web-app WebSocket heartbeat just arrived. */
void wifi_ap_note_heartbeat(void);

/**
 * @brief Declare an OTA in progress (keeps the AP on) or finished.
 *
 * @param active true while an OTA is running.
 */
void wifi_ap_set_ota_active(bool active);

/** @brief Request the AP to switch on (e.g. both-Shift combo, M3). */
void wifi_ap_request_on(void);

/** @brief Re-read the stored password and restart the AP so it takes effect (clients are dropped). */
void wifi_ap_apply_password(void);

/** @brief Whether wifi_ap_init() completed (used by the OTA self-test). */
bool wifi_ap_is_initialized(void);

#ifdef __cplusplus
}
#endif
