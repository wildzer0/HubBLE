/**
 * @file bridge.h
 * @brief Glue between the BLE side and the USB side: report forwarding, combos, LED sync.
 *
 * Responsibilities (CLAUDE.md §3.1 architecture, §3.3, §3.5, §3.10, §3.11):
 *  - decode every report from ble_central with hid_normalizer and feed usb_hid as source = device slot;
 *  - release all keys/buttons of a device when it disconnects;
 *  - measure notification -> USB-report-queued latency per device;
 *  - run the combo detector on the merged keyboard state (Ctrl + Alt -> Wi-Fi AP on; Ctrl + Alt + Esc for
 *    10 s -> reset the Wi-Fi password);
 *  - forward the host's keyboard LED state to the BLE keyboards.
 * The forwarding path allocates nothing and never blocks on Wi-Fi/HTTP work.
 * Dependencies: ble_central, usb_hid, hid_normalizer, combo, wifi_ap, settings.
 * Threading: on_report runs in the NimBLE host task; the LED callback runs in the TinyUSB task; the combo task
 * is its own task.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "power_policy.h"

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

/**
 * @brief Install the input-tester tap (CLAUDE.md §3.7 page 5).
 *
 * While @p active returns true, every decoded, non-quarantined report is also copied to a small queue and sent as an
 * `input.event` through @p emit from a low-priority task (the forwarding path never waits for it; events are
 * dropped when the queue is full). Nothing is stored or logged. Call before bridge_init().
 *
 * @param active Returns true while a web client wants the stream (called on the BLE host task: must be cheap).
 * @param emit   Receives the `data` JSON object of each event (called on the tap task).
 */
void bridge_set_input_tap(bool (*active)(void), void (*emit)(const char *json));

/**
 * @brief Current power mode (low while the host has been suspended for longer than the debounce time).
 *
 * @return POWER_ACTIVE or POWER_LOW.
 */
power_mode_t bridge_power_mode(void);

/**
 * @brief Configure the typing filter (bounce keys / slow keys) applied to keys and mouse buttons before the USB report.
 *
 * Effective immediately; thread-safe. When disabled the reports pass through untouched and with no added latency. When
 * enabled a hold time adds that much delay to every press (that is its purpose).
 *
 * @param enabled   Master switch.
 * @param bounce_ms Ignore a key or button pressed again this soon after its release (0 = off).
 * @param hold_ms   A key or button counts only after being held this long (0 = off).
 */
void bridge_set_input_filter(bool enabled, uint32_t bounce_ms, uint32_t hold_ms);

#ifdef __cplusplus
}
#endif
