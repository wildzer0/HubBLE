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

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of URI handlers api_register() adds (so the server can size max_uri_handlers). */
#define API_URI_HANDLER_COUNT 8

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
