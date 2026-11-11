/**
 * @file web_server.c
 * @brief HTTP + WebSocket server implementation. See web_server.h.
 *
 * Dependencies: esp_http_server (HTTPD_WS_SUPPORT), api, web_assets, wifi_ap, log_hook, cJSON.
 * Threading: HTTP handlers on the server task; one extra task pushes periodic events. The server handle is
 * written once at start-up.
 */
#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "api.h"
#include "board.h"
#include "cJSON.h"
#include "diag.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_hook.h"
#include "web_assets.h"
#include "wifi_ap.h"

static const char *TAG = "web";

/** Stack of the HTTP server task: cJSON trees and the OTA handler need more than the 4 KB default. */
#define HTTPD_STACK 8192
/** Extra URI handlers beyond the REST API: WebSocket, static files, plus spare. */
#define EXTRA_URI_HANDLERS 4
/** Maximum WebSocket clients tracked (keeps the broadcast fd array small). */
#define MAX_WS_FDS 8
/** Largest WebSocket text frame accepted from a client. */
#define WS_MAX_FRAME 128
/** Period of the status/log push task. */
#define PUSH_PERIOD_MS 1000
/** Push task stack. */
#define PUSH_STACK 4096
/** Push task priority. */
#define PUSH_PRIO 3
/** Chunk of log text sent per `log.line` event. */
#define LOG_PUSH_MAX 512
/** URL the captive-portal redirect points to (the default SoftAP address). */
#define PORTAL_URL "http://192.168.4.1/"
/** Cache lifetime for hashed assets under /assets/ (Vite emits content-hashed file names). */
#define CACHE_IMMUTABLE "public, max-age=31536000, immutable"

/** Server handle; NULL until started. */
static httpd_handle_t s_server;
/** Next log sequence to push over the WebSocket. */
static uint64_t s_log_cursor;

/**
 * @brief Whether the Host header names this dongle (IP or mDNS name, optional port).
 *
 * @param host Host header value or NULL.
 * @return true if requests for it should be served; false if they look like captive-portal probes.
 */
static bool host_is_ours(const char *host) {
    if (host == NULL) {
        return true; /* HTTP/1.0 clients without Host: serve normally. */
    }
    return strncmp(host, "192.168.4.1", 11) == 0 || strncasecmp(host, "hubble.local", 12) == 0;
}

/**
 * @brief Pick a Content-Type from a file extension.
 *
 * @param path File path.
 * @return MIME type string.
 */
static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return "application/octet-stream";
    }
    static const struct {
        const char *ext;
        const char *mime;
    } table[] = {
        {".html", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".ico", "image/x-icon"},
        {".txt", "text/plain; charset=utf-8"},
        {".woff2", "font/woff2"},
        {".webmanifest", "application/manifest+json"},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(dot, table[i].ext) == 0) {
            return table[i].mime;
        }
    }
    return "application/octet-stream";
}

/**
 * @brief Catch-all GET handler: portal redirect, static files, SPA fallback.
 *
 * @param req Request.
 * @return ESP_OK.
 */
static esp_err_t static_get(httpd_req_t *req) {
    char host[64] = {0};
    const bool has_host = httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK;
    if (!host_is_ours(has_host ? host : NULL)) {
        /* OS connectivity probes (Android generate_204, iOS hotspot-detect, Windows connecttest) land here with a
         * foreign Host. Redirecting makes the OS pop up the captive-portal browser (CLAUDE.md §3.5). */
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", PORTAL_URL);
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, NULL, 0);
    }

    char path[96];
    const char *q = strchr(req->uri, '?');
    size_t len = q ? (size_t)(q - req->uri) : strlen(req->uri);
    if (len >= sizeof(path)) {
        return httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, NULL);
    }
    memcpy(path, req->uri, len);
    path[len] = '\0';
    const char *file = path[0] == '/' ? path + 1 : path;
    if (file[0] == '\0') {
        file = "index.html";
    }

    size_t blob_len;
    const uint8_t *blob = web_assets_get(&blob_len);
    web_bundle_entry_t e;
    int found = web_bundle_find(blob, blob_len, file, &e);
    const char *served = file;
    if (found != 1) {
        if (strncmp(path, "/api/", 5) == 0) {
            httpd_resp_set_status(req, "404 Not Found");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"error\":{\"code\":\"not_found\",\"message\":\"Unknown endpoint\"}}");
        }
        served = "index.html"; /* Single-page-app fallback for client-side routes. */
        found = web_bundle_find(blob, blob_len, served, &e);
    }
    if (found != 1) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
    }
    httpd_resp_set_type(req, mime_for(served));
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", strncmp(served, "assets/", 7) == 0 ? CACHE_IMMUTABLE : "no-cache");
    return httpd_resp_send(req, (const char *)e.data, (ssize_t)e.size);
}

/**
 * @brief WebSocket handler: handshake and the client `hello` heartbeat.
 *
 * @param req Request.
 * @return ESP_OK, or an error that makes the server close the socket.
 */
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket client connected");
        return ESP_OK; /* Handshake is completed by the server. */
    }
    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len == 0 || frame.len > WS_MAX_FRAME) {
        return frame.len == 0 ? ESP_OK : ESP_FAIL; /* Oversized frames are a protocol violation. */
    }
    uint8_t buf[WS_MAX_FRAME + 1];
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        buf[frame.len] = '\0';
        cJSON *j = cJSON_Parse((const char *)buf);
        const cJSON *ev = j ? cJSON_GetObjectItemCaseSensitive(j, "event") : NULL;
        if (cJSON_IsString(ev) && strcmp(ev->valuestring, "hello") == 0) {
            wifi_ap_note_heartbeat(); /* Keeps the AP alive while the web app is open (CLAUDE.md §3.5). */
        }
        cJSON_Delete(j);
    }
    return ESP_OK;
}

/** @copydoc web_server_broadcast */
void web_server_broadcast(const char *event, const char *data_json) {
    if (s_server == NULL) {
        return;
    }
    size_t nfds = MAX_WS_FDS * 2;
    int fds[MAX_WS_FDS * 2];
    if (httpd_get_client_list(s_server, &nfds, fds) != ESP_OK) {
        return;
    }
    const size_t cap = strlen(event) + strlen(data_json) + 32;
    char *msg = malloc(cap);
    if (msg == NULL) {
        return;
    }
    const int n = snprintf(msg, cap, "{\"event\":\"%s\",\"data\":%s}", event, data_json);
    for (size_t i = 0; i < nfds && n > 0; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)msg, .len = (size_t)n};
            httpd_ws_send_frame_async(s_server, fds[i], &f);
        }
    }
    free(msg);
}

/** @brief Whether at least one WebSocket client is connected. */
static bool any_ws_client(void) {
    size_t nfds = MAX_WS_FDS * 2;
    int fds[MAX_WS_FDS * 2];
    if (s_server == NULL || httpd_get_client_list(s_server, &nfds, fds) != ESP_OK) {
        return false;
    }
    for (size_t i = 0; i < nfds; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Periodic push task: `system.status` and `log.line` events.
 *
 * @param arg Unused.
 */
static void push_task(void *arg) {
    (void)arg;
    char text[LOG_PUSH_MAX];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PUSH_PERIOD_MS));
        if (!any_ws_client()) {
            s_log_cursor = log_hook_end(); /* Nobody listens: skip ahead instead of flooding the first client. */
            continue;
        }
        wifi_ap_status_t ap;
        wifi_ap_get_status(&ap);
        diag_info_t d;
        diag_collect(&d);
        cJSON *st = cJSON_CreateObject();
        cJSON_AddNumberToObject(st, "uptime_s", d.uptime_s);
        cJSON_AddNumberToObject(st, "heap_free", d.heap_free);
        if (ap.timeout_s >= 0) {
            cJSON_AddNumberToObject(st, "ap_timeout_s", ap.timeout_s);
        } else {
            cJSON_AddNullToObject(st, "ap_timeout_s");
        }
        char *s = cJSON_PrintUnformatted(st);
        web_server_broadcast("system.status", s);
        cJSON_free(s);
        cJSON_Delete(st);

        uint64_t next;
        const size_t n = log_hook_read(s_log_cursor, text, sizeof(text) - 1, true, &next);
        s_log_cursor = next;
        if (n > 0) {
            text[n] = '\0';
            cJSON *l = cJSON_CreateObject();
            cJSON_AddStringToObject(l, "text", text);
            char *ls = cJSON_PrintUnformatted(l);
            web_server_broadcast("log.line", ls);
            cJSON_free(ls);
            cJSON_Delete(l);
        }
    }
}

/** @copydoc web_server_is_running */
bool web_server_is_running(void) {
    return s_server != NULL;
}

/** @copydoc web_server_start */
esp_err_t web_server_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = HTTPD_STACK;
    cfg.max_uri_handlers = API_URI_HANDLER_COUNT + EXTRA_URI_HANDLERS;
    cfg.uri_match_fn = httpd_uri_match_wildcard; /* Needed for the catch-all route. */
    cfg.lru_purge_enable = true;                 /* Phones leave idle sockets open; recycle the oldest. */
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 5;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }
    /* Order matters: exact API routes and the WebSocket first, the catch-all route last. */
    err = api_register(s_server);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t ws = {.uri = "/api/v1/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
    err = httpd_register_uri_handler(s_server, &ws);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t any = {.uri = "/*", .method = HTTP_GET, .handler = static_get};
    err = httpd_register_uri_handler(s_server, &any);
    if (err != ESP_OK) {
        return err;
    }
    s_log_cursor = log_hook_end();
    if (xTaskCreate(push_task, "ws_push", PUSH_STACK, NULL, PUSH_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "HTTP server up on port %d", cfg.server_port);
    return ESP_OK;
}
