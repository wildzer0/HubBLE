/**
 * @file wifi_scan_list.h
 * @brief Pure post-processing of a Wi-Fi scan for the "pick a network" list (host-testable).
 *
 * Raw scans contain hidden networks (empty SSID), one entry per access point (a mesh or a dual-band router appears
 * many times) and arrive in no useful order. The web app wants one line per network name, strongest first, limited
 * to a screenful.
 * Dependencies: libc only. Threading: none (operates on caller buffers).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest SSID (IEEE 802.11) without the terminator is 32 bytes. */
#define WIFI_SCAN_SSID_MAX 33
/** Most networks returned to the web app. */
#define WIFI_SCAN_LIST_MAX 20

/** One network of the list. */
typedef struct {
    char ssid[WIFI_SCAN_SSID_MAX]; /**< NUL-terminated network name. */
    int8_t rssi;                   /**< Signal strength in dBm (higher is stronger). */
    bool secure;                   /**< true unless the network is open (no password needed). */
} wifi_scan_net_t;

/**
 * @brief Clean a raw scan list in place: drop hidden networks, keep only the strongest entry per SSID, sort by
 *        signal (strongest first, ties by SSID) and truncate to @p max entries.
 *
 * @param list Entries to process; rewritten in place.
 * @param n    Number of valid entries in @p list.
 * @param max  Maximum number of entries to keep.
 * @return The number of entries left in @p list (at most @p max and at most @p n).
 */
size_t wifi_scan_list_clean(wifi_scan_net_t *list, size_t n, size_t max);

#ifdef __cplusplus
}
#endif
