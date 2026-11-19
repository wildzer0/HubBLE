/**
 * @file remote_input.h
 * @brief The phone as a touchpad and keyboard: pointer, scroll, button, key and text events from the web app.
 *
 * Responsibilities:
 *  - keep the "remote input" feature OFF until the web app switches it on for a limited time (default 5 minutes,
 *    never persisted: a restart turns it off);
 *  - turn `remote.*` WebSocket messages into USB input of the dedicated merge source HID_SRC_REMOTE;
 *  - type text from a queue in a task (key down, pause, key up), so the HTTP task never waits;
 *  - release everything it holds when the window ends, the input is locked, or the phone goes quiet (a closed page
 *    cannot leave a button or key stuck on the host).
 * Security: anyone with the Wi-Fi password could type on the host, so the window must be opened on purpose, only
 * WebSocket clients are accepted (not the USB link) and nothing is accepted while the input lock is on. Nothing typed
 * is logged or stored (CLAUDE.md §3.12).
 *
 * Messages (JSON, `event` plus fields): `remote.move` {dx, dy}; `remote.scroll` {dx, dy} (dy > 0 scrolls up);
 * `remote.buttons` {b} (bit 0 left, 1 right, 2 middle: the buttons held now); `remote.key` {m, k: [usages]} (the
 * modifiers and keys held now; empty releases); `remote.text` {t, m} (typed with the US layout, see ascii_hid.h, with
 * the modifier byte `m` held for every character).
 *
 * Dependencies: usb_hid, bridge (lock), cJSON. Threading: remote_input_handle() runs in the HTTP server task; one
 * private task types and watches; the enable state is guarded by a critical section.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How long one activation lasts, seconds. */
#define REMOTE_INPUT_WINDOW_S 300

/**
 * @brief Create the typing/watchdog task. Call once, after usb_hid_init().
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t remote_input_init(void);

/**
 * @brief Switch the feature on for REMOTE_INPUT_WINDOW_S seconds (again: restarts the window).
 */
void remote_input_enable(void);

/**
 * @brief Switch the feature off and release everything it holds on the host.
 */
void remote_input_disable(void);

/**
 * @brief Seconds left in the current window.
 *
 * @return 0 when the feature is off.
 */
uint32_t remote_input_remaining_s(void);

/**
 * @brief Handle one `remote.*` client message.
 *
 * Ignored silently when the feature is off or the input is locked. Values are clamped; malformed fields are ignored.
 *
 * @param event Event name (starts with "remote.").
 * @param msg   Parsed message.
 */
void remote_input_handle(const char *event, const cJSON *msg);

#ifdef __cplusplus
}
#endif
