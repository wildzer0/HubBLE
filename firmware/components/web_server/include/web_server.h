/**
 * @file web_server.h
 * @brief HTTP server: embedded web app, REST API, WebSocket events, captive-portal redirects.
 *
 * Responsibilities (CLAUDE.md §3.5, §3.7, §4.2):
 *  - serve the gzipped web app from the firmware image with `Content-Encoding: gzip`;
 *  - host the REST API (api component) and the WebSocket `/api/v1/ws`;
 *  - redirect requests for foreign host names (OS captive-portal probes) to the app.
 * Dependencies: esp_http_server, api, web_assets, wifi_ap, log_buffer, cJSON.
 * Threading: handlers run on the HTTP server task; web_server_broadcast() may be called from any task.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP server and the periodic event task.
 *
 * @return ESP_OK or the first error.
 */
esp_err_t web_server_start(void);

/**
 * @brief Whether the server is running (OTA self-test).
 *
 * @return true if started successfully.
 */
bool web_server_is_running(void);

/**
 * @brief Send an event to every connected WebSocket client.
 *
 * Message shape: `{"event":"<name>","data":<json>}`.
 *
 * @param event     Event name, e.g. "ota.progress".
 * @param data_json Already-serialised JSON value for `data`.
 */
void web_server_broadcast(const char *event, const char *data_json);

/**
 * @brief Mirror every event to a second listener (the USB configuration link).
 *
 * @param sink   Called for each event the WebSocket clients would get (input events only while the USB listener holds
 * an input-tester subscription). Must not block; may be called from any task. NULL removes it.
 * @param wanted Whether the listener wants periodic events right now (keeps the status/log push running when no
 * WebSocket client is connected). May be NULL.
 */
void web_server_set_event_sink(void (*sink)(const char *event, const char *data_json), bool (*wanted)(void));

/**
 * @brief Handle a message from the USB link as if it had arrived on a WebSocket (`hello`, `input.subscribe`, ...).
 *
 * @param json The JSON text the web app would have sent.
 */
void web_server_usb_message(const char *json);

/**
 * @brief Whether at least one web client currently subscribes to `input.event` (input tester).
 *
 * Cheap (one timestamp compare), safe to call from any task including the BLE host task. Subscriptions expire 15 s
 * after the last `input.subscribe` frame, so a closed page stops the stream by itself; the web app renews the
 * subscription with every heartbeat.
 *
 * @return true while the stream is wanted.
 */
bool web_server_input_active(void);

/**
 * @brief Send an `input.event` to the subscribed WebSocket clients only.
 *
 * @param data_json JSON object for the `data` field. Never stored (CLAUDE.md §3.12).
 */
void web_server_broadcast_input(const char *data_json);

#ifdef __cplusplus
}
#endif
