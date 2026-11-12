/**
 * @file api.h
 * @brief REST API handlers under `/api/v1/` (CLAUDE.md §4.1).
 *
 * Responsibilities: register the REST endpoints on an existing HTTP server and implement them on top of
 * the other components. Errors are `{ "error": { "code": "...", "message": "..." } }`.
 * Every state-changing call is protected by http_guard (CSRF).
 * Dependencies: esp_http_server, cJSON, ota, diag, settings, wifi_ap, log_buffer.
 * Threading: handlers run in the HTTP server task; they never block on HID work (there is none yet).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of URI handlers api_register() adds (so the server can size max_uri_handlers). */
#if CONFIG_HUBBLE_USB_DEBUG_INJECT
#define API_URI_HANDLER_COUNT 25
#else
#define API_URI_HANDLER_COUNT 24
#endif

/**
 * @brief Build the JSON payload of a `device.updated` / `device.removed` WebSocket event.
 *
 * @param slot Device slot.
 * @param out  Output buffer.
 * @param cap  Buffer size.
 * @return true if the JSON was produced (false for an invalid slot or a too small buffer).
 */
bool api_device_event_json(int slot, char *out, size_t cap);

/**
 * @brief Emit the WebSocket events describing the current pairing state: `pairing.state` always, plus
 *        `pairing.passkey`, `pairing.challenge` or `pairing.result` when applicable (all carry the status object).
 *
 * @param emit Called once per event with the event name and its JSON payload.
 */
void api_pairing_events(void (*emit)(const char *event, const char *json));

/**
 * @brief Emit the `ota.state` WebSocket event (online update engine state, same object as `online` in
 *        `GET /api/v1/ota/status`).
 *
 * @param emit Function receiving the event name and the JSON `data` object.
 */
void api_ota_events(void (*emit)(const char *event, const char *json));

/**
 * @brief Emit the `ota.state` WebSocket event (online update engine state, same object as `online` in
 *        `GET /api/v1/ota/status`).
 *
 * @param emit Function receiving the event name and the JSON `data` object.
 */
void api_ota_events(void (*emit)(const char *event, const char *json));

/**
 * @brief Register all REST handlers.
 *
 * @param server Running HTTP server.
 * @return ESP_OK or the first registration error.
 */
esp_err_t api_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
