/**
 * @file api.c
 * @brief REST API implementation. See api.h and docs/api/openapi.yaml.
 *
 * Dependencies: cJSON, esp_http_server, ota, diag, settings, wifi_ap, log_hook.
 * Threading: handlers run on the HTTP server task (8 KB stack); the OTA upload handler streams through a
 * static buffer, so only one upload can run at a time (enforced by ota_session_begin()).
 */
#include "api.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cJSON.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "http_guard.h"
#include "log_hook.h"
#include "ota.h"
#include "settings.h"
#include "wifi_ap.h"

static const char *TAG = "api";

/** Largest JSON request body accepted. */
#define MAX_JSON_BODY 512
/** Upload receive chunk size: a multiple of the flash sector keeps writes efficient. */
#define UPLOAD_CHUNK 4096
/** Delay before a deferred action runs, so the HTTP response is flushed first. */
#define DEFER_MS 1500
/** Size of the buffer used for the log download. */
#define LOG_CHUNK 1024
/** Header carrying the CSRF token. */
#define HDR_CSRF "X-HubBLE"
/** Content types. */
#define CT_JSON  "application/json"
#define CT_OCTET "application/octet-stream"

/** Deferred action kinds. */
typedef enum {
    DEFER_REBOOT,
    DEFER_REBOOT_DOWNLOAD,
    DEFER_APPLY_PASSWORD,
} defer_kind_t;

/** Receive buffer for uploads (static: handler stacks are small; only one upload at a time). */
static uint8_t s_upload_buf[UPLOAD_CHUNK];

/**
 * @brief esp_timer callback performing a deferred action.
 *
 * @param arg The defer_kind_t, cast through intptr_t.
 */
static void defer_cb(void *arg) {
    switch ((defer_kind_t)(intptr_t)arg) {
    case DEFER_REBOOT:
        esp_restart();
        break;
    case DEFER_REBOOT_DOWNLOAD:
        diag_reboot_to_download();
        break;
    case DEFER_APPLY_PASSWORD:
        wifi_ap_apply_password();
        break;
    }
}

/**
 * @brief Run an action after DEFER_MS so the current HTTP response can be sent first.
 *
 * @param kind Action.
 */
static void defer(defer_kind_t kind) {
    esp_timer_handle_t t;
    const esp_timer_create_args_t args = {.callback = defer_cb, .arg = (void *)(intptr_t)kind, .name = "defer"};
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, DEFER_MS * 1000ULL);
    }
}

/**
 * @brief Send a JSON error body.
 *
 * @param req    Request.
 * @param status HTTP status line, e.g. "400 Bad Request".
 * @param code   Machine readable code.
 * @param msg    Human readable message.
 * @return ESP_OK (the error has been answered).
 */
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *code, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    char *text = cJSON_PrintUnformatted(root);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, CT_JSON);
    httpd_resp_sendstr(req, text);
    cJSON_free(text);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Serialise and send a cJSON tree (takes ownership of @p root).
 *
 * @param req  Request.
 * @param root JSON to send; deleted here.
 * @return Result of the send.
 */
static esp_err_t send_json(httpd_req_t *req, cJSON *root) {
    char *text = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, CT_JSON);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t r = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    cJSON_Delete(root);
    return r;
}

/**
 * @brief Read a request header into @p buf, or return NULL if absent/too long.
 */
static const char *get_hdr(httpd_req_t *req, const char *name, char *buf, size_t cap) {
    return httpd_req_get_hdr_value_str(req, name, buf, cap) == ESP_OK ? buf : NULL;
}

/**
 * @brief Apply the CSRF guard; on failure the error response has been sent.
 *
 * @param req Request.
 * @param expected_ct Required content type.
 * @return true if the request may proceed.
 */
static bool guard(httpd_req_t *req, const char *expected_ct) {
    char x[8], ct[64], origin[96], host[64];
    const guard_result_t g = http_guard_check(
        get_hdr(req, HDR_CSRF, x, sizeof(x)), get_hdr(req, "Content-Type", ct, sizeof(ct)), expected_ct,
        get_hdr(req, "Origin", origin, sizeof(origin)), get_hdr(req, "Host", host, sizeof(host)));
    switch (g) {
    case GUARD_OK:
        return true;
    case GUARD_ERR_CONTENT_TYPE:
        send_error(req, "415 Unsupported Media Type", "bad_content_type", "Unexpected Content-Type");
        break;
    case GUARD_ERR_HEADER:
    case GUARD_ERR_ORIGIN:
        send_error(req, "403 Forbidden", "forbidden", "Missing X-HubBLE header or cross-origin request");
        break;
    }
    return false;
}

/** GET /api/v1/status. */
static esp_err_t status_get(httpd_req_t *req) {
    wifi_ap_status_t ap;
    wifi_ap_get_status(&ap);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(r, "board", board_get_info()->id);
    cJSON_AddNumberToObject(r, "uptime_s", (double)(esp_timer_get_time() / 1000000LL));
    cJSON_AddStringToObject(r, "usb_state", "detached"); /* USB device arrives in M2. */
    if (ap.timeout_s >= 0) {
        cJSON_AddNumberToObject(r, "ap_timeout_s", ap.timeout_s);
    } else {
        cJSON_AddNullToObject(r, "ap_timeout_s");
    }
    cJSON_AddBoolToObject(r, "setup_required", !settings_is_setup_done());
    return send_json(req, r);
}

/** GET /api/v1/ota/status. */
static esp_err_t ota_status_get(httpd_req_t *req) {
    ota_status_t s;
    ota_get_status(&s);
    cJSON *r = cJSON_CreateObject();
    cJSON *run = cJSON_AddObjectToObject(r, "running");
    cJSON_AddStringToObject(run, "slot", s.running_slot);
    cJSON_AddStringToObject(run, "version", s.running_version);
    cJSON *other = cJSON_AddObjectToObject(r, "other");
    cJSON_AddStringToObject(other, "slot", s.other_slot);
    cJSON_AddStringToObject(other, "version", s.other_version);
    cJSON_AddBoolToObject(r, "pending_verify", s.pending_verify);
    cJSON_AddBoolToObject(r, "rollback_happened", s.rollback_happened);
    cJSON_AddNumberToObject(r, "boot_failures", s.boot_failures);
    cJSON_AddStringToObject(r, "last_result", s.last_result);
    return send_json(req, r);
}

/** @brief Add a partition object to @p parent. */
static void add_partition(cJSON *parent, const char *name, const diag_partition_t *p) {
    cJSON *o = cJSON_AddObjectToObject(parent, name);
    cJSON_AddStringToObject(o, "label", p->label);
    cJSON_AddNumberToObject(o, "address", p->address);
    cJSON_AddNumberToObject(o, "size", p->size);
}

/** GET /api/v1/diagnostics. */
static esp_err_t diagnostics_get(httpd_req_t *req) {
    diag_info_t d;
    diag_collect(&d);
    wifi_ap_status_t ap;
    wifi_ap_get_status(&ap);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "uptime_s", d.uptime_s);
    cJSON_AddStringToObject(r, "reset_reason", d.reset_reason);
    cJSON *heap = cJSON_AddObjectToObject(r, "heap");
    cJSON_AddNumberToObject(heap, "free", d.heap_free);
    cJSON_AddNumberToObject(heap, "min_free", d.heap_min_free);
    cJSON_AddNumberToObject(heap, "largest_block", d.heap_largest_block);
    cJSON *ps = cJSON_AddObjectToObject(r, "psram");
    cJSON_AddNumberToObject(ps, "total", d.psram_total);
    cJSON_AddNumberToObject(ps, "free", d.psram_free);
    cJSON *parts = cJSON_AddObjectToObject(r, "partitions");
    add_partition(parts, "running", &d.running);
    add_partition(parts, "next_update", &d.next_update);
    cJSON_AddNumberToObject(r, "boot_failures", settings_boot_failures());
    cJSON_AddStringToObject(r, "usb_state", "detached");
    cJSON_AddNullToObject(r, "ble"); /* BLE arrives in M3. */
    cJSON *wifi = cJSON_AddObjectToObject(r, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", ap.ssid);
    cJSON_AddNumberToObject(wifi, "clients", ap.clients);
    return send_json(req, r);
}

/** GET /api/v1/logs: the RAM log buffer as plain text. */
static esp_err_t logs_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"hubble.log\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buf[LOG_CHUNK];
    uint64_t cursor = 0;
    const uint64_t end = log_hook_end(); /* Snapshot the end so a busy logger cannot make this loop forever. */
    while (cursor < end) {
        uint64_t next;
        const size_t n = log_hook_read(cursor, buf, sizeof(buf), false, &next);
        if (n == 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
            return ESP_FAIL;
        }
        cursor = next;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/**
 * @brief Read the whole request body (<= MAX_JSON_BODY) and parse it as JSON.
 *
 * @param req Request.
 * @return Parsed JSON, or NULL after sending an error response.
 */
static cJSON *read_json_body(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > MAX_JSON_BODY) {
        send_error(req, "400 Bad Request", "bad_body", "Body missing or too large");
        return NULL;
    }
    char buf[MAX_JSON_BODY];
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            send_error(req, "400 Bad Request", "bad_body", "Could not read body");
            return NULL;
        }
        got += (size_t)n;
    }
    cJSON *j = cJSON_ParseWithLength(buf, got);
    if (j == NULL) {
        send_error(req, "400 Bad Request", "bad_json", "Body is not valid JSON");
    }
    return j;
}

/** POST /api/v1/settings/wifi-password. */
static esp_err_t wifi_password_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *pw = cJSON_GetObjectItemCaseSensitive(j, "password");
    if (!cJSON_IsString(pw) || !settings_password_valid(pw->valuestring)) {
        cJSON_Delete(j);
        return send_error(req, "400 Bad Request", "invalid_password",
                          "Password must be 10-63 printable characters and not the default");
    }
    const esp_err_t err = settings_set_ap_password(pw->valuestring);
    cJSON_Delete(j);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "storage_error", "Could not store the password");
    }
    defer(DEFER_APPLY_PASSWORD); /* Applying drops the current Wi-Fi connection, so do it after replying. */
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** @brief Map an OTA error to an HTTP status/code and send it. */
static esp_err_t send_ota_error(httpd_req_t *req, ota_result_t r) {
    const char *status = "400 Bad Request";
    const char *code = "ota_failed";
    switch (r) {
    case OTA_ERR_BUSY:
        status = "409 Conflict";
        code = "ota_busy";
        break;
    case OTA_ERR_BOARD_MISMATCH:
        status = "422 Unprocessable Entity";
        code = "board_mismatch";
        break;
    case OTA_ERR_BAD_IMAGE:
        code = "bad_image";
        break;
    case OTA_ERR_BAD_SIZE:
        status = "413 Payload Too Large";
        code = "bad_size";
        break;
    case OTA_ERR_VALIDATION:
        status = "422 Unprocessable Entity";
        code = "validation_failed";
        break;
    case OTA_ERR_FLASH:
        status = "500 Internal Server Error";
        code = "flash_error";
        break;
    case OTA_OK:
        break;
    }
    return send_error(req, status, code, ota_result_text(r));
}

/** POST /api/v1/ota/upload: raw .bin streamed straight to flash. */
static esp_err_t ota_upload_post(httpd_req_t *req) {
    if (!guard(req, CT_OCTET)) {
        return ESP_OK;
    }
    ota_result_t r = ota_session_begin(req->content_len);
    if (r != OTA_OK) {
        return send_ota_error(req, r);
    }
    size_t remaining = req->content_len;
    while (remaining > 0) {
        const size_t want = remaining < sizeof(s_upload_buf) ? remaining : sizeof(s_upload_buf);
        const int n = httpd_req_recv(req, (char *)s_upload_buf, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue; /* Slow phone link: keep waiting; the server's recv timeout only bounds each read. */
        }
        if (n <= 0) {
            ota_session_abort();
            ESP_LOGE(TAG, "upload interrupted");
            return ESP_FAIL; /* Closes the connection; the client already lost it. */
        }
        r = ota_session_write(s_upload_buf, (size_t)n);
        if (r != OTA_OK) {
            return send_ota_error(req, r);
        }
        remaining -= (size_t)n;
    }
    char version[33];
    r = ota_session_finish(version, sizeof(version));
    if (r != OTA_OK) {
        return send_ota_error(req, r);
    }
    defer(DEFER_REBOOT);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    cJSON_AddStringToObject(j, "version", version);
    cJSON_AddBoolToObject(j, "reboot", true);
    return send_json(req, j);
}

/** POST /api/v1/system/reboot. */
static esp_err_t reboot_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    defer(DEFER_REBOOT);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** POST /api/v1/system/reboot-download. */
static esp_err_t reboot_download_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    defer(DEFER_REBOOT_DOWNLOAD);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** @copydoc api_register */
esp_err_t api_register(httpd_handle_t server) {
    static const httpd_uri_t routes[API_URI_HANDLER_COUNT] = {
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/v1/ota/status", .method = HTTP_GET, .handler = ota_status_get},
        {.uri = "/api/v1/diagnostics", .method = HTTP_GET, .handler = diagnostics_get},
        {.uri = "/api/v1/logs", .method = HTTP_GET, .handler = logs_get},
        {.uri = "/api/v1/settings/wifi-password", .method = HTTP_POST, .handler = wifi_password_post},
        {.uri = "/api/v1/ota/upload", .method = HTTP_POST, .handler = ota_upload_post},
        {.uri = "/api/v1/system/reboot", .method = HTTP_POST, .handler = reboot_post},
        {.uri = "/api/v1/system/reboot-download", .method = HTTP_POST, .handler = reboot_download_post},
    };
    for (size_t i = 0; i < API_URI_HANDLER_COUNT; i++) {
        const esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot register %s: %s", routes[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}
