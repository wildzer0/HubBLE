/**
 * @file captive_portal.h
 * @brief Captive-portal DNS server and mDNS (`http://hubble.local`).
 *
 * Responsibilities: answer every DNS query on the AP with the dongle's IP so that phones open
 * the web app automatically, and announce `hubble.local` over mDNS (CLAUDE.md §3.5).
 * The HTTP side (redirecting probe URLs to the app) lives in web_server.
 * Dependencies: lwip sockets, mdns, esp_netif. Threading: owns one task.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** mDNS host name (without ".local"). */
#define CAPTIVE_PORTAL_HOSTNAME "hubble"

/**
 * @brief Start the DNS responder task and mDNS. Call after wifi_ap_init().
 *
 * @return ESP_OK or the first error.
 */
esp_err_t captive_portal_start(void);

#ifdef __cplusplus
}
#endif
