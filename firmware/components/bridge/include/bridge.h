/**
 * @file bridge.h
 * @brief Glue between the BLE side and the USB side: report forwarding, combos, LED sync.
 *
 * Responsibilities (CLAUDE.md §3.1 architecture, §3.3, §3.5, §3.10, §3.11):
 *  - decode every report from ble_central with hid_normalizer and feed usb_hid as source = device slot;
 *  - release all keys/buttons of a device when it disconnects;
 *  - measure notification -> USB-report-queued latency per device;
 *  - run the combo detector on the merged keyboard state (both Shifts -> Wi-Fi AP on; both Shifts + Esc for
 *    10 s -> reset the Wi-Fi password);
 *  - forward the host's keyboard LED state to the BLE keyboards.
 * The forwarding path allocates nothing and never blocks on Wi-Fi/HTTP work.
 * Dependencies: ble_central, usb_hid, hid_normalizer, combo, wifi_ap, settings.
 * Threading: on_report runs in the NimBLE host task; the LED callback runs in the TinyUSB task; the combo task
 * is its own task.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the BLE central and the bridge.
 *
 * @param on_changed Called (from any task) when a device or the pairing state changed; slot is -1 for the
 *                   pairing flow. May be NULL.
 * @return ESP_OK or the error from ble_central_init().
 */
esp_err_t bridge_init(void (*on_changed)(int slot));

#ifdef __cplusplus
}
#endif
