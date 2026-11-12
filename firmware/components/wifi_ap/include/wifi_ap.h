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
#include "wifi_scan_list.h"

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

/**
 * @brief Join a Wi-Fi network as a station (APSTA mode) for "check for updates" (CLAUDE.md §3.5).
 *
 * The AP must be running; the caller keeps it on with wifi_ap_set_ota_active(true). Joining makes the radio follow the
 * router's channel, so the AP may move to that channel and phones can briefly disconnect: this is a known APSTA
 * limitation, documented in docs/ota-and-recovery.md.
 *
 * @param ssid       Network name.
 * @param password   Passphrase, "" for an open network.
 * @param timeout_ms How long to wait for an IP address.
 * @return ESP_OK once an IP address was obtained; ESP_ERR_TIMEOUT if the network cannot be joined;
 *         ESP_ERR_INVALID_STATE if the AP is not running.
 */
esp_err_t wifi_ap_sta_connect(const char *ssid, const char *password, uint32_t timeout_ms);

/**
 * @brief Scan for nearby Wi-Fi networks, for the "pick a network" list of the web app. Blocks for a couple of seconds.
 *
 * Switches to APSTA for the duration of the scan and back to AP-only afterwards. The AP keeps its channel, but the
 * radio is busy hopping channels, so phones on the AP may see short stalls (not disconnections). Hidden networks are
 * left out, names are deduplicated and the list is sorted by signal (see wifi_scan_list_clean()).
 *
 * @param[out] out   Receives up to @p max networks.
 * @param max        Capacity of @p out (use WIFI_SCAN_LIST_MAX).
 * @param[out] count Number of networks written.
 * @return ESP_OK; ESP_ERR_INVALID_STATE if the AP is off, a scan or a join is already running; ESP_ERR_NO_MEM or a
 *         Wi-Fi driver error otherwise.
 */
esp_err_t wifi_ap_scan(wifi_scan_net_t *out, size_t max, size_t *count);

/** @brief Leave the station network and return to AP-only mode. Safe to call when not connected. */
void wifi_ap_sta_disconnect(void);

/**
 * @brief Tell the AP manager that the host suspended (or resumed) the USB bus: the AP is switched off while suspended
 *        (unless an OTA runs) and the policy resumes afterwards (CLAUDE.md §3.9).
 *
 * @param suspended true while the bus is suspended (after the power debounce).
 */
void wifi_ap_set_host_suspended(bool suspended);

#ifdef __cplusplus
}
#endif
