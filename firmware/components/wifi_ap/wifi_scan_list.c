/**
 * @file wifi_scan_list.c
 * @brief Scan list clean-up. See wifi_scan_list.h.
 *
 * Dependencies: libc. Threading: none.
 */
#include "wifi_scan_list.h"

#include <string.h>

/**
 * @brief Ordering for the final list: stronger signal first, then by SSID so the result is deterministic.
 *
 * @param a First entry.
 * @param b Second entry.
 * @return true if @p a must come before @p b.
 */
static bool before(const wifi_scan_net_t *a, const wifi_scan_net_t *b) {
    if (a->rssi != b->rssi) {
        return a->rssi > b->rssi;
    }
    return strcmp(a->ssid, b->ssid) < 0;
}

/** @copydoc wifi_scan_list_clean */
size_t wifi_scan_list_clean(wifi_scan_net_t *list, size_t n, size_t max) {
    /* Pass 1: compact away hidden networks and merge duplicates, keeping the strongest AP of each name (its security
     * is the one the user would join). The list is small (a few dozen), so quadratic is fine and needs no memory. */
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if (list[i].ssid[0] == '\0') {
            continue;
        }
        size_t dup = out;
        for (size_t j = 0; j < out; j++) {
            if (strcmp(list[j].ssid, list[i].ssid) == 0) {
                dup = j;
                break;
            }
        }
        if (dup == out) {
            list[out++] = list[i];
        } else if (list[i].rssi > list[dup].rssi) {
            list[dup] = list[i];
        }
    }
    /* Pass 2: insertion sort (stable, in place). */
    for (size_t i = 1; i < out; i++) {
        const wifi_scan_net_t cur = list[i];
        size_t j = i;
        while (j > 0 && before(&cur, &list[j - 1])) {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = cur;
    }
    return out > max ? max : out;
}
