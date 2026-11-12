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

#include "ble_central.h"
#include "board.h"
#include "bridge.h"
#include "cJSON.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "http_guard.h"
#include "log_hook.h"
#include "ota.h"
#include "ota_online.h"
#include "nvs_flash.h"
#include "settings.h"
#include "usb_hid.h"
#include "wifi_ap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "api";

/** Largest JSON request body accepted. */
#define MAX_JSON_BODY 512
/** Upload receive chunk size: a multiple of the flash sector keeps writes efficient. */
#define UPLOAD_CHUNK 4096
/** Duration of the pairing scan in ms (CLAUDE.md §3.4: pairing lasts at most 60 s). */
#define PAIRING_SCAN_MS 30000
/** Largest report map exported (matches the client's buffer). */
#define HOGP_MAP_MAX_EXPORT 640
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
    DEFER_FACTORY_RESET,
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
        usb_hid_shutdown(); /* Release the USB PHY so the ROM downloader can use it after the reset. */
        diag_reboot_to_download();
        break;
    case DEFER_APPLY_PASSWORD:
        wifi_ap_apply_password();
        break;
    case DEFER_FACTORY_RESET:
        /* Erases settings, the Wi-Fi password, the device registry and the NimBLE bonds (all live in NVS), then
         * restarts into the first-run state. Nothing else touches NVS after this point. */
        nvs_flash_deinit();
        nvs_flash_erase();
        esp_restart();
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
    /* The Host must name the dongle too (blocks DNS rebinding); a missing Host is a non-browser HTTP/1.0 client. */
    if (g == GUARD_OK && host[0] != '\0' && !http_guard_host_ours(host)) {
        send_error(req, "403 Forbidden", "forbidden", "Missing X-HubBLE header or cross-origin request");
        return false;
    }
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
    cJSON_AddStringToObject(r, "usb_state", usb_hid_state_name(usb_hid_get_state()));
    if (ap.timeout_s >= 0) {
        cJSON_AddNumberToObject(r, "ap_timeout_s", ap.timeout_s);
    } else {
        cJSON_AddNullToObject(r, "ap_timeout_s");
    }
    cJSON_AddBoolToObject(r, "setup_required", !settings_is_setup_done());
    return send_json(req, r);
}

/**
 * @brief Describe the online update engine (REST `online` object and `ota.state` event).
 *
 * @return New cJSON object (caller owns it).
 */
static cJSON *online_json(void) {
    ou_status_t u;
    ota_online_get_status(&u);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", ota_online_state_name(u.state));
    if (u.state == OU_ERROR) {
        cJSON_AddStringToObject(o, "error", ota_online_error_name(u.error));
    }
    cJSON_AddBoolToObject(o, "wifi_configured", settings_has_sta());
    cJSON_AddStringToObject(o, "channel", u.prerelease_channel ? "prerelease" : "stable");
    if (u.have_release) {
        cJSON *r = cJSON_AddObjectToObject(o, "release");
        cJSON_AddStringToObject(r, "version", u.release.version);
        cJSON_AddStringToObject(r, "min_version", u.release.min_version);
        cJSON_AddStringToObject(r, "notes_url", u.release.notes_url);
        cJSON_AddNumberToObject(r, "size", u.release.size);
        cJSON_AddBoolToObject(r, "signed", u.release.signed_image);
        cJSON_AddBoolToObject(r, "downgrade", u.decision == OTA_DECISION_DOWNGRADE);
    }
    if (u.state == OU_DOWNLOADING || u.state == OU_INSTALLED) {
        cJSON_AddNumberToObject(o, "received", u.received);
        cJSON_AddNumberToObject(o, "total", u.total);
    }
    if (u.state == OU_AWAIT_CONFIRM) {
        cJSON_AddNumberToObject(o, "confirm_remaining_s", u.confirm_remaining_s);
    }
    return o;
}

/** @copydoc api_ota_events */
void api_ota_events(void (*emit)(const char *event, const char *json)) {
    cJSON *o = online_json();
    char *text = cJSON_PrintUnformatted(o);
    if (text != NULL) {
        emit("ota.state", text);
        cJSON_free(text);
    }
    cJSON_Delete(o);
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
    cJSON_AddItemToObject(r, "online", online_json());
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
    float temp_c;
    if (diag_chip_temperature(&temp_c)) {
        cJSON_AddNumberToObject(r, "chip_temp_c", (double)((int)(temp_c * 10.0f)) / 10.0);
    }
    cJSON_AddStringToObject(r, "usb_state", usb_hid_state_name(usb_hid_get_state()));
    cJSON_AddNumberToObject(r, "keyboard_leds", usb_hid_get_leds());
    cJSON *ble = cJSON_AddObjectToObject(r, "ble");
    cJSON_AddBoolToObject(ble, "ready", ble_central_is_ready());
    cJSON_AddNumberToObject(ble, "connected", ble_central_connected_count());
    cJSON_AddNumberToObject(ble, "bonded", ble_central_bonded_count());
    cJSON *pw = cJSON_AddObjectToObject(r, "power");
    cJSON_AddStringToObject(pw, "mode", bridge_power_mode() == POWER_LOW ? "low" : "active");
    cJSON_AddBoolToObject(pw, "remote_wakeup", settings_get_remote_wakeup());
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

/**
 * POST /api/v1/system/wifi-off: switch the AP off now (it stays off until the Ctrl + Alt gesture or a restart).
 * Refused while no device is paired: the AP is then the only way to pair one.
 */
static esp_err_t wifi_off_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    if (ble_central_bonded_count() == 0) {
        return send_error(req, "409 Conflict", "no_bonded_devices",
                          "Pair a device first: the Wi-Fi stays on while none is paired");
    }
    wifi_ap_request_off();
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

/* ---- Devices and pairing (CLAUDE.md §4.1) --------------------------------------------------------- */

/** URI prefix of the per-device routes. */
#define DEVICES_PREFIX "/api/v1/devices/"
/** Longest accepted device id (12 hex digits). */
#define DEVICE_ID_LEN 12

/** @brief Security badge name for the API (green = passkey/SC, yellow = Just Works verified, red = legacy). */
static const char *sec_name(sec_level_t s) {
    switch (s) {
    case SEC_GREEN:
        return "green";
    case SEC_YELLOW:
        return "yellow";
    case SEC_RED:
        return "red";
    case SEC_UNKNOWN:
    default:
        return "unknown";
    }
}

/**
 * @brief Build the JSON object describing a device.
 *
 * @param d Device snapshot.
 * @return New cJSON object (caller owns it).
 */
static cJSON *device_json(const ble_device_info_t *d) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", d->id);
    cJSON_AddNumberToObject(o, "slot", d->slot);
    cJSON_AddStringToObject(o, "name", d->name);
    cJSON_AddStringToObject(o, "type", !d->ready ? "unknown" : (d->keyboard ? "keyboard" : "pointer"));
    cJSON_AddBoolToObject(o, "connected", d->connected);
    cJSON_AddBoolToObject(o, "ready", d->ready);
    cJSON_AddBoolToObject(o, "boot_fallback", d->boot_fallback);
    if (d->battery == BLE_BATTERY_UNKNOWN) {
        cJSON_AddNullToObject(o, "battery");
    } else {
        cJSON_AddNumberToObject(o, "battery", d->battery);
    }
    if (d->connected) {
        cJSON_AddNumberToObject(o, "rssi", d->rssi);
    } else {
        cJSON_AddNullToObject(o, "rssi");
    }
    cJSON *conn = cJSON_AddObjectToObject(o, "connection");
    cJSON_AddNumberToObject(conn, "interval_ms", d->conn_itvl_125us * 1.25);
    cJSON_AddNumberToObject(conn, "latency", d->conn_latency);
    cJSON_AddNumberToObject(conn, "timeout_ms", d->conn_timeout_10ms * 10);
    cJSON_AddNumberToObject(o, "reconnects", d->reconnects);
    cJSON_AddNumberToObject(o, "last_seen_s", d->last_seen_s);
    cJSON *lat = cJSON_AddObjectToObject(o, "latency_us");
    cJSON_AddNumberToObject(lat, "count", d->latency.count);
    cJSON_AddNumberToObject(lat, "min", d->latency.min_us);
    cJSON_AddNumberToObject(lat, "avg", d->latency.avg_us);
    cJSON_AddNumberToObject(lat, "p95", d->latency.p95_us);
    cJSON_AddNumberToObject(lat, "max", d->latency.max_us);
    cJSON_AddStringToObject(o, "security", sec_name(d->security));
    return o;
}

/** @copydoc api_device_event_json */
bool api_device_event_json(int slot, char *out, size_t cap) {
    ble_device_info_t d;
    if (slot < 0 || !ble_central_get_device((unsigned)slot, &d)) {
        return false;
    }
    cJSON *o;
    if (d.in_use) {
        o = device_json(&d);
    } else {
        o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "slot", slot);
        cJSON_AddBoolToObject(o, "removed", true);
    }
    bool ok = cJSON_PrintPreallocated(o, out, (int)cap, false);
    cJSON_Delete(o);
    return ok;
}

/** GET /api/v1/devices. */
static esp_err_t devices_get(httpd_req_t *req) {
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(r, "devices");
    for (unsigned i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        ble_device_info_t d;
        if (ble_central_get_device(i, &d) && d.in_use) {
            cJSON_AddItemToArray(arr, device_json(&d));
        }
    }
    cJSON_AddNumberToObject(r, "max", BLE_CENTRAL_MAX_DEVICES);
    return send_json(req, r);
}

/**
 * @brief Extract the device id (and the optional sub-path) from a request URI and find its slot.
 *
 * @param req        Request.
 * @param[out] suffix Receives a pointer to the text after the id ("" or "/report-map").
 * @return Slot, or -1 after sending a 404.
 */
static int device_from_uri(httpd_req_t *req, const char **suffix) {
    const char *p = req->uri + strlen(DEVICES_PREFIX);
    char id[DEVICE_ID_LEN + 1];
    size_t n = 0;
    while (p[n] != '\0' && p[n] != '/' && p[n] != '?' && n < DEVICE_ID_LEN) {
        id[n] = p[n];
        n++;
    }
    id[n] = '\0';
    *suffix = p + n;
    int slot = ble_central_find_by_id(id);
    if (slot < 0) {
        send_error(req, "404 Not Found", "not_found", "Unknown device");
    }
    return slot;
}

/** Add a string field, or null when it is empty. */
static void add_str_or_null(cJSON *o, const char *key, const char *value) {
    if (value != NULL && value[0] != '\0') {
        cJSON_AddStringToObject(o, key, value);
    } else {
        cJSON_AddNullToObject(o, key);
    }
}

/**
 * GET /api/v1/devices/{id}/report: the compatibility report (docs/api/device-report.schema.json). It holds what the
 * dongle learned about the device, never keystrokes, the Bluetooth address, the user's name for the device or any Wi-Fi
 * data. The web app shows it to the user before anything is sent.
 */
static esp_err_t device_report_get(httpd_req_t *req, int slot) {
    ble_device_info_t d;
    ble_report_info_t ri;
    if (!ble_central_get_device((unsigned)slot, &d) || !ble_central_get_report_info((unsigned)slot, &ri)) {
        return send_error(req, "409 Conflict", "not_ready", "The device must be connected to build a report");
    }
    uint8_t map[HOGP_MAP_MAX_EXPORT];
    const size_t n = ble_central_get_report_map((unsigned)slot, map, sizeof(map));
    char *hex = cJSON_malloc(n * 2 + 1);
    if (hex == NULL) {
        return send_error(req, "500 Internal Server Error", "no_memory", "Out of memory");
    }
    for (size_t i = 0; i < n; i++) {
        snprintf(&hex[i * 2], 3, "%02x", map[i]);
    }
    hex[n * 2] = '\0';

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "format", "hubble-device-report");
    cJSON_AddNumberToObject(r, "version", 1);
    cJSON *fw = cJSON_AddObjectToObject(r, "firmware");
    cJSON_AddStringToObject(fw, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(fw, "board", board_get_info()->id);
    cJSON *dev = cJSON_AddObjectToObject(r, "device");
    cJSON_AddStringToObject(dev, "kind", ri.keyboard ? "keyboard" : (ri.pointer ? "pointer" : "unknown"));
    add_str_or_null(dev, "adv_name", ri.info.device_name);
    if (ri.info.has_appearance) {
        cJSON_AddNumberToObject(dev, "appearance", ri.info.appearance);
    } else {
        cJSON_AddNullToObject(dev, "appearance");
    }
    add_str_or_null(dev, "manufacturer", ri.info.manufacturer);
    add_str_or_null(dev, "model", ri.info.model);
    add_str_or_null(dev, "fw_revision", ri.info.fw_revision);
    if (ri.info.has_pnp) {
        cJSON *pnp = cJSON_AddObjectToObject(dev, "pnp");
        cJSON_AddNumberToObject(pnp, "vendor_id_source", ri.info.pnp_source);
        cJSON_AddNumberToObject(pnp, "vendor_id", ri.info.pnp_vendor);
        cJSON_AddNumberToObject(pnp, "product_id", ri.info.pnp_product);
        cJSON_AddNumberToObject(pnp, "version", ri.info.pnp_version);
    } else {
        cJSON_AddNullToObject(dev, "pnp");
    }
    cJSON_AddStringToObject(dev, "report_map_hex", hex);
    cJSON_free(hex);
    cJSON *parse = cJSON_AddObjectToObject(dev, "parse");
    cJSON_AddNumberToObject(parse, "result", ri.parse_result);
    cJSON_AddNumberToObject(parse, "fields", ri.fields);
    cJSON_AddBoolToObject(parse, "boot_fallback", ri.boot_fallback);
    cJSON_AddNumberToObject(parse, "primary_page", ri.primary_page);
    cJSON_AddNumberToObject(parse, "primary_usage", ri.primary_usage);
    cJSON_AddBoolToObject(parse, "has_pointer", ri.pointer);
    cJSON_AddStringToObject(dev, "security", sec_name(ri.security));
    cJSON *conn = cJSON_AddObjectToObject(dev, "connection");
    cJSON_AddNumberToObject(conn, "interval_ms", d.conn_itvl_125us * 1.25);
    cJSON_AddNumberToObject(conn, "latency", d.conn_latency);
    cJSON_AddNumberToObject(conn, "timeout_ms", d.conn_timeout_10ms * 10);
    cJSON_AddBoolToObject(dev, "battery_reported", ri.battery_reported);
    cJSON *lat = cJSON_AddObjectToObject(dev, "latency_us");
    cJSON_AddNumberToObject(lat, "count", d.latency.count);
    cJSON_AddNumberToObject(lat, "avg", d.latency.avg_us);
    cJSON_AddNumberToObject(lat, "p95", d.latency.p95_us);
    cJSON_AddNumberToObject(lat, "max", d.latency.max_us);
    return send_json(req, r);
}

/** GET /api/v1/devices/{id}/report-map: the raw report map as hex (debugging, test fixtures). */
static esp_err_t device_item_get(httpd_req_t *req) {
    const char *suffix;
    int slot = device_from_uri(req, &suffix);
    if (slot < 0) {
        return ESP_OK;
    }
    if (strncmp(suffix, "/report", 7) == 0 && strncmp(suffix, "/report-map", 11) != 0) {
        return device_report_get(req, slot);
    }
    if (strncmp(suffix, "/report-map", 11) != 0) {
        ble_device_info_t d;
        ble_central_get_device((unsigned)slot, &d);
        return send_json(req, device_json(&d));
    }
    uint8_t map[HOGP_MAP_MAX_EXPORT];
    size_t n = ble_central_get_report_map((unsigned)slot, map, sizeof(map));
    if (n == 0) {
        return send_error(req, "409 Conflict", "not_ready", "Report map not available (device not connected)");
    }
    char *hex = cJSON_malloc(n * 2 + 1);
    if (hex == NULL) {
        return send_error(req, "500 Internal Server Error", "no_memory", "Out of memory");
    }
    for (size_t i = 0; i < n; i++) {
        snprintf(&hex[i * 2], 3, "%02x", map[i]);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "hex", hex);
    cJSON_AddNumberToObject(r, "length", (double)n);
    cJSON_free(hex);
    return send_json(req, r);
}

/** PATCH /api/v1/devices/{id}: rename. */
static esp_err_t device_item_patch(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    const char *suffix;
    int slot = device_from_uri(req, &suffix);
    if (slot < 0) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
    bool ok = cJSON_IsString(name) && name->valuestring[0] != '\0';
    if (ok) {
        ble_central_rename((unsigned)slot, name->valuestring);
    }
    cJSON_Delete(j);
    if (!ok) {
        return send_error(req, "400 Bad Request", "bad_name", "name must be a non-empty string");
    }
    ble_device_info_t d;
    ble_central_get_device((unsigned)slot, &d);
    return send_json(req, device_json(&d));
}

/** DELETE /api/v1/devices/{id}: forget the device and delete its bond. */
static esp_err_t device_item_delete(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    const char *suffix;
    int slot = device_from_uri(req, &suffix);
    if (slot < 0) {
        return ESP_OK;
    }
    ble_central_forget((unsigned)slot);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** @brief Name of a pairing state for the API. */
static const char *pair_state_name(pair_state_t s) {
    switch (s) {
    case PAIR_SCANNING:
        return "scanning";
    case PAIR_CONNECTING:
        return "connecting";
    case PAIR_PAIRING:
        return "pairing";
    case PAIR_QUARANTINE:
        return "quarantine";
    case PAIR_CONFIRM:
        return "confirm";
    case PAIR_BONDED:
        return "bonded";
    case PAIR_FAILED:
        return "failed";
    case PAIR_IDLE:
    default:
        return "idle";
    }
}

/** @brief Failure reason name for the API. */
static const char *fail_name(pair_fail_t f) {
    switch (f) {
    case FAIL_TIMEOUT:
        return "timeout";
    case FAIL_CANCELLED:
        return "cancelled";
    case FAIL_LEGACY:
        return "legacy_rejected";
    case FAIL_SECURITY:
        return "security_failed";
    case FAIL_NO_HID:
        return "no_hid_service";
    case FAIL_CHALLENGE:
        return "challenge_failed";
    case FAIL_NONE:
    default:
        return "none";
    }
}

/**
 * @brief Build the pairing status object (shared by REST and WebSocket).
 *
 * @return New cJSON object (caller owns it).
 */
static cJSON *pairing_json(void) {
    ble_pairing_status_t s;
    ble_central_pairing_status(&s);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", pair_state_name(s.state));
    if (s.state == PAIR_FAILED) {
        cJSON_AddStringToObject(o, "reason", fail_name(s.fail));
    }
    cJSON_AddStringToObject(o, "level", sec_name(s.level));
    cJSON_AddStringToObject(o, "device", s.name);
    cJSON_AddNumberToObject(o, "remaining_s", (s.remaining_ms + 999) / 1000);
    cJSON_AddNumberToObject(o, "lock_remaining_s", s.lock_remaining_s);
    cJSON_AddNumberToObject(o, "rx_reports", s.rx_reports);
    cJSON_AddBoolToObject(o, "needs_confirmation", s.need_confirm);
    if (s.has_passkey) {
        char pk[PAIR_PASSKEY_DIGITS + 1];
        snprintf(pk, sizeof(pk), "%06u", (unsigned)s.passkey); /* zero padded: the user types all six digits */
        cJSON_AddStringToObject(o, "passkey", pk);
    }
    if (s.challenge_kind != CHALLENGE_NONE) {
        cJSON *ch = cJSON_AddObjectToObject(o, "challenge");
        cJSON_AddStringToObject(ch, "kind", s.challenge_kind == CHALLENGE_MOUSE ? "clicks" : "code");
        cJSON_AddStringToObject(ch, "value", s.challenge);
        cJSON_AddNumberToObject(ch, "progress", s.challenge_pos);
        cJSON_AddNumberToObject(ch, "length", s.challenge_len);
    }
    return o;
}

/** @copydoc api_pairing_events */
void api_pairing_events(void (*emit)(const char *event, const char *json)) {
    ble_pairing_status_t s;
    ble_central_pairing_status(&s);
    cJSON *o = pairing_json();
    char *text = cJSON_PrintUnformatted(o);
    if (text != NULL) {
        emit("pairing.state", text);
        if (s.has_passkey) {
            emit("pairing.passkey", text);
        }
        if (s.state == PAIR_QUARANTINE) {
            emit("pairing.challenge", text);
        }
        if (s.state == PAIR_BONDED || s.state == PAIR_FAILED) {
            emit("pairing.result", text);
        }
        cJSON_free(text);
    }
    cJSON_Delete(o);
}

/** GET /api/v1/pairing/scan: pairing state and the scan list. */
static esp_err_t pairing_scan_get(httpd_req_t *req) {
    ble_scan_result_t res[BLE_CENTRAL_MAX_SCAN];
    size_t n = ble_central_scan_results(res);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "state", pair_state_name(ble_central_pair_state()));
    cJSON_AddItemToObject(r, "pairing", pairing_json());
    cJSON *arr = cJSON_AddArrayToObject(r, "results");
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        char addr[13];
        for (int b = 0; b < 6; b++) {
            snprintf(&addr[b * 2], 3, "%02x", res[i].addr[b]);
        }
        cJSON_AddStringToObject(o, "address", addr);
        cJSON_AddNumberToObject(o, "address_type", res[i].addr_type);
        cJSON_AddStringToObject(o, "name", res[i].name);
        cJSON_AddNumberToObject(o, "rssi", res[i].rssi);
        cJSON_AddNumberToObject(o, "appearance", res[i].appearance);
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, r);
}

/** POST /api/v1/pairing/scan: start scanning for HID devices. */
static esp_err_t pairing_scan_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    esp_err_t err = ble_central_scan_start(PAIRING_SCAN_MS);
    if (err == ESP_ERR_NOT_ALLOWED) {
        return send_error(req, "429 Too Many Requests", "pairing_locked",
                          "Pairing is locked for 60 s after 3 failed attempts");
    }
    if (err == ESP_ERR_NO_MEM) {
        return send_error(req, "409 Conflict", "no_free_slot", "Four devices are already paired; forget one first");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", "busy", "Bluetooth is busy or not ready");
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/**
 * @brief Parse 12 hex digits into a 6-byte address (display order).
 *
 * @param s   String.
 * @param out Receives the address.
 * @return true if valid.
 */
static bool parse_address(const char *s, uint8_t out[6]) {
    if (s == NULL || strlen(s) != 12) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        unsigned v;
        if (sscanf(&s[i * 2], "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

/** POST /api/v1/pairing/start {"address":"aabbccddeeff","address_type":0}. */
static esp_err_t pairing_start_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(j, "address");
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(j, "address_type");
    uint8_t addr[6];
    bool ok = cJSON_IsString(a) && parse_address(a->valuestring, addr);
    uint8_t type = cJSON_IsNumber(t) ? (uint8_t)t->valueint : 0;
    const cJSON *lg = cJSON_GetObjectItemCaseSensitive(j, "allow_legacy");
    bool allow_legacy = cJSON_IsTrue(lg); /* Per-pairing override; results in security level red. */
    cJSON_Delete(j);
    if (!ok) {
        return send_error(req, "400 Bad Request", "bad_address", "address must be 12 hex digits");
    }
    esp_err_t err = ble_central_pair(addr, type, allow_legacy);
    if (err == ESP_ERR_NOT_ALLOWED) {
        return send_error(req, "429 Too Many Requests", "pairing_locked",
                          "Pairing is locked for 60 s after 3 failed attempts");
    }
    if (err == ESP_ERR_NO_MEM) {
        return send_error(req, "409 Conflict", "no_free_slot", "Four devices are already paired; forget one first");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", "busy", "Pairing already running, or the device is already paired");
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** POST /api/v1/pairing/cancel. */
static esp_err_t pairing_cancel_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    ble_central_pair_cancel();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/**
 * @brief Build the settings object (shared by GET /settings and GET /backup).
 *
 * @return New cJSON object (caller owns it).
 */
static cJSON *settings_json(void) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "require_confirmation", settings_get_require_confirm());
    cJSON_AddNumberToObject(r, "ap_boot_window_s", settings_get_boot_window_s());
    cJSON_AddNumberToObject(r, "ap_idle_timeout_s", settings_get_idle_timeout_s());
    cJSON_AddStringToObject(r, "update_channel", settings_get_prerelease_channel() ? "prerelease" : "stable");
    cJSON_AddBoolToObject(r, "sta_configured", settings_has_sta()); /* Never the credentials themselves. */
    cJSON_AddBoolToObject(r, "remote_wakeup", settings_get_remote_wakeup());
    cJSON_AddBoolToObject(r, "portal_auto", settings_get_portal_auto());
    cJSON_AddNumberToObject(r, "led_brightness", settings_get_led_brightness());
    cJSON_AddBoolToObject(r, "status_led", settings_get_status_led());
    cJSON_AddNumberToObject(r, "shift_hold_ms", settings_get_shift_hold_ms());
    return r;
}

/**
 * @brief Whether every present field of a settings object is valid. Nothing is stored here.
 *
 * @param s Settings object (may lack fields).
 * @return true if all present fields have the right type and range.
 */
static bool settings_object_valid(const cJSON *s) {
    const cJSON *rc = cJSON_GetObjectItemCaseSensitive(s, "require_confirmation");
    const cJSON *bw = cJSON_GetObjectItemCaseSensitive(s, "ap_boot_window_s");
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(s, "ap_idle_timeout_s");
    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(s, "update_channel");
    if (rc != NULL && !cJSON_IsBool(rc)) {
        return false;
    }
    const cJSON *rw = cJSON_GetObjectItemCaseSensitive(s, "remote_wakeup");
    const cJSON *sl = cJSON_GetObjectItemCaseSensitive(s, "status_led");
    const cJSON *lb = cJSON_GetObjectItemCaseSensitive(s, "led_brightness");
    const cJSON *sh = cJSON_GetObjectItemCaseSensitive(s, "shift_hold_ms");
    const cJSON *pa = cJSON_GetObjectItemCaseSensitive(s, "portal_auto");
    if ((rw != NULL && !cJSON_IsBool(rw)) || (sl != NULL && !cJSON_IsBool(sl)) || (pa != NULL && !cJSON_IsBool(pa))) {
        return false;
    }
    if (lb != NULL && !(cJSON_IsNumber(lb) && lb->valuedouble == (double)lb->valueint && lb->valueint >= 0 &&
                        lb->valueint <= SETTINGS_LED_BRIGHTNESS_MAX)) {
        return false;
    }
    if (sh != NULL && !(cJSON_IsNumber(sh) && sh->valuedouble == (double)sh->valueint &&
                        sh->valueint >= SETTINGS_SHIFT_HOLD_MIN_MS && sh->valueint <= SETTINGS_SHIFT_HOLD_MAX_MS)) {
        return false;
    }
    if (ch != NULL && !(cJSON_IsString(ch) &&
                        (strcmp(ch->valuestring, "stable") == 0 || strcmp(ch->valuestring, "prerelease") == 0))) {
        return false;
    }
    if (bw != NULL && !(cJSON_IsNumber(bw) && bw->valuedouble == (double)bw->valueint && bw->valueint > 0 &&
                        settings_window_valid((uint32_t)bw->valueint))) {
        return false;
    }
    if (it != NULL && !(cJSON_IsNumber(it) && it->valuedouble == (double)it->valueint && it->valueint > 0 &&
                        settings_window_valid((uint32_t)it->valueint))) {
        return false;
    }
    return true;
}

/**
 * @brief Apply the present fields of an already validated settings object.
 *
 * @param s Settings object.
 */
static void settings_object_apply(const cJSON *s) {
    const cJSON *rc = cJSON_GetObjectItemCaseSensitive(s, "require_confirmation");
    const cJSON *bw = cJSON_GetObjectItemCaseSensitive(s, "ap_boot_window_s");
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(s, "ap_idle_timeout_s");
    if (cJSON_IsBool(rc)) {
        bool on = cJSON_IsTrue(rc);
        settings_set_require_confirm(on);
        ble_central_set_require_confirm(on);
    }
    if (cJSON_IsNumber(bw)) {
        settings_set_boot_window_s((uint32_t)bw->valueint);
    }
    if (cJSON_IsNumber(it)) {
        settings_set_idle_timeout_s((uint32_t)it->valueint);
    }
    const cJSON *rw = cJSON_GetObjectItemCaseSensitive(s, "remote_wakeup");
    const cJSON *sl = cJSON_GetObjectItemCaseSensitive(s, "status_led");
    const cJSON *lb = cJSON_GetObjectItemCaseSensitive(s, "led_brightness");
    const cJSON *sh = cJSON_GetObjectItemCaseSensitive(s, "shift_hold_ms");
    if (cJSON_IsBool(rw)) {
        settings_set_remote_wakeup(cJSON_IsTrue(rw));
        usb_hid_set_remote_wakeup_allowed(cJSON_IsTrue(rw)); /* Effective immediately. */
    }
    const cJSON *pa = cJSON_GetObjectItemCaseSensitive(s, "portal_auto");
    if (cJSON_IsBool(pa)) {
        settings_set_portal_auto(cJSON_IsTrue(pa));
    }
    if (cJSON_IsBool(sl)) {
        settings_set_status_led(cJSON_IsTrue(sl));
    }
    if (cJSON_IsNumber(lb)) {
        settings_set_led_brightness((uint32_t)lb->valueint);
    }
    if (cJSON_IsNumber(sh)) {
        settings_set_shift_hold_ms(
            (uint32_t)sh->valueint); /* Applies from the next boot (the combo task reads it once). */
    }
    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(s, "update_channel");
    if (cJSON_IsString(ch)) {
        settings_set_prerelease_channel(strcmp(ch->valuestring, "prerelease") == 0);
    }
}

/** GET /api/v1/settings: non-secret settings (the AP windows apply from the next boot). */
static esp_err_t settings_get(httpd_req_t *req) {
    return send_json(req, settings_json());
}

/** PUT /api/v1/settings: update the given fields; all-or-nothing validation. */
static esp_err_t settings_put(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    if (!settings_object_valid(j)) {
        cJSON_Delete(j);
        return send_error(req, "400 Bad Request", "invalid_setting", "A setting is missing, mistyped or out of range");
    }
    settings_object_apply(j);
    cJSON_Delete(j);
    return settings_get(req);
}

/**
 * GET /api/v1/backup: settings as a downloadable document. Bonds and the Wi-Fi password are deliberately NOT included:
 * they are secrets (CLAUDE.md §3.7 page 8, §3.12).
 */
static esp_err_t backup_get(httpd_req_t *req) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "format", "hubble-backup");
    cJSON_AddNumberToObject(r, "version", 1);
    cJSON_AddItemToObject(r, "settings", settings_json());
    return send_json(req, r);
}

/** POST /api/v1/restore: apply a backup document produced by backup_get(). */
static esp_err_t restore_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *fmt = cJSON_GetObjectItemCaseSensitive(j, "format");
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(j, "version");
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(j, "settings");
    const bool ok = cJSON_IsString(fmt) && strcmp(fmt->valuestring, "hubble-backup") == 0 && cJSON_IsNumber(ver) &&
                    ver->valueint == 1 && cJSON_IsObject(st) && settings_object_valid(st) &&
                    cJSON_GetObjectItemCaseSensitive(st, "require_confirmation") != NULL &&
                    cJSON_GetObjectItemCaseSensitive(st, "ap_boot_window_s") != NULL &&
                    cJSON_GetObjectItemCaseSensitive(st, "ap_idle_timeout_s") != NULL;
    if (!ok) {
        cJSON_Delete(j);
        return send_error(req, "400 Bad Request", "bad_backup", "Not a valid HubBLE backup");
    }
    settings_object_apply(st);
    cJSON_Delete(j);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** PUT /api/v1/settings/sta {"ssid","password"}: save (or, with an empty ssid, forget) the update Wi-Fi network. */
static esp_err_t settings_sta_put(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(j, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(j, "password");
    const bool shape = cJSON_IsString(ssid) && (pass == NULL || cJSON_IsString(pass));
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (shape) {
        err = settings_set_sta(ssid->valuestring, pass != NULL ? pass->valuestring : "");
    }
    cJSON_Delete(j);
    if (err != ESP_OK) {
        return send_error(req, "400 Bad Request", "invalid_sta",
                          "SSID must be 1-32 characters; the password empty or 8-63 printable characters");
    }
    return settings_get(req); /* Reports sta_configured only. */
}

/** GET /api/v1/wifi/scan: nearby Wi-Fi networks for the "Wi-Fi network for updates" picker. */
static esp_err_t wifi_scan_get(httpd_req_t *req) {
    /* A GET has no CSRF header, but the scan is slow and exposes the neighbourhood: still require our own Host. */
    char host[64];
    if (get_hdr(req, "Host", host, sizeof(host)) != NULL && !http_guard_host_ours(host)) {
        return send_error(req, "403 Forbidden", "forbidden", "Cross-origin request");
    }
    ou_status_t u;
    ota_online_get_status(&u);
    const ble_pair_state_t ps = ble_central_pair_state();
    const bool update_busy = u.state == OU_CONNECTING || u.state == OU_CHECKING || u.state == OU_AWAIT_CONFIRM ||
                             u.state == OU_DOWNLOADING || u.state == OU_INSTALLED;
    const bool pairing_busy = ps == PAIR_SCANNING || ps == PAIR_CONNECTING || ps == PAIR_PAIRING;
    if (update_busy || pairing_busy) {
        return send_error(req, "503 Service Unavailable", "busy",
                          "The radio is busy with an update or a pairing; try again in a moment");
    }
    wifi_scan_net_t nets[WIFI_SCAN_LIST_MAX];
    size_t n = 0;
    const esp_err_t err = wifi_ap_scan(nets, WIFI_SCAN_LIST_MAX, &n);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "503 Service Unavailable", "busy", "Another Wi-Fi operation is running");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "scan_failed", "The Wi-Fi scan failed");
    }
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(r, "networks");
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", nets[i].ssid); /* cJSON escapes the name. */
        cJSON_AddNumberToObject(o, "rssi", nets[i].rssi);
        cJSON_AddBoolToObject(o, "secure", nets[i].secure);
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, r);
}

/** POST /api/v1/ota/check {"channel":"stable"|"prerelease"}: start looking for an update on GitHub. */
static esp_err_t ota_check_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(j, "channel");
    const bool pre =
        cJSON_IsString(ch) ? strcmp(ch->valuestring, "prerelease") == 0 : settings_get_prerelease_channel();
    cJSON_Delete(j);
    const esp_err_t err = ota_online_check(pre);
    if (err == ESP_ERR_NOT_FOUND) {
        return send_error(req, "409 Conflict", "no_wifi_credentials", "Save the Wi-Fi network used for updates first");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", "busy",
                          "An update operation is already running, or the Wi-Fi AP is off");
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** POST /api/v1/ota/install {"allow_downgrade":bool}: install the release found by the last check. */
static esp_err_t ota_install_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const bool down = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "allow_downgrade"));
    cJSON_Delete(j);
    const esp_err_t err = ota_online_install(down);
    if (err == ESP_ERR_NOT_ALLOWED) {
        return send_error(req, "409 Conflict", "downgrade_not_confirmed", "This release is older than the running one");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", "nothing_to_install",
                          "Check for updates first, or an install is running");
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

/** POST /api/v1/system/factory-reset: erase everything and restart (the response is sent first). */
static esp_err_t factory_reset_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    defer(DEFER_FACTORY_RESET);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

#if CONFIG_HUBBLE_USB_DEBUG_INJECT
/** Longest time a debug-injected key/button is held, ms (it blocks the HTTP task while held). */
#define DEBUG_HOLD_MAX_MS 1000
/** Default hold time of a debug-injected key/button, ms. */
#define DEBUG_HOLD_DEFAULT_MS 50

/**
 * @brief Read an optional integer field from a JSON object.
 *
 * @param o   Object.
 * @param key Field name.
 * @param def Value returned when the field is missing or not a number.
 * @return The value, truncated to int.
 */
static int json_int(const cJSON *o, const char *key, int def) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

/**
 * POST /api/v1/debug/input (debug builds only): inject keyboard / mouse / consumer / system input as the
 * debug source, so the USB side can be verified without any BLE device. Bodies:
 *  - {"kind":"key","modifiers":0,"keys":[4,5],"hold_ms":50}
 *  - {"kind":"mouse","buttons":1,"dx":10,"dy":0,"wheel":0,"pan":0,"hold_ms":50}
 *  - {"kind":"consumer","usage":233,"hold_ms":50}
 *  - {"kind":"system","bits":1,"hold_ms":50}
 * The state is applied, held for hold_ms and then released.
 */
static esp_err_t debug_input_post(httpd_req_t *req) {
    if (!guard(req, CT_JSON)) {
        return ESP_OK;
    }
    cJSON *j = read_json_body(req);
    if (j == NULL) {
        return ESP_OK;
    }
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(j, "kind");
    int hold = json_int(j, "hold_ms", DEBUG_HOLD_DEFAULT_MS);
    hold = hold < 0 ? 0 : (hold > DEBUG_HOLD_MAX_MS ? DEBUG_HOLD_MAX_MS : hold);
    bool ok = true;
    if (cJSON_IsString(kind) && strcmp(kind->valuestring, "key") == 0) {
        uint8_t keys[HID_KBD_MAX_KEYS];
        size_t n = 0;
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(j, "keys");
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (cJSON_IsNumber(it) && n < HID_KBD_MAX_KEYS) {
                keys[n++] = (uint8_t)it->valueint;
            }
        }
        usb_hid_keyboard(HID_SRC_DEBUG, (uint8_t)json_int(j, "modifiers", 0), keys, n);
        vTaskDelay(pdMS_TO_TICKS(hold));
        usb_hid_keyboard(HID_SRC_DEBUG, 0, NULL, 0);
    } else if (cJSON_IsString(kind) && strcmp(kind->valuestring, "mouse") == 0) {
        const uint8_t buttons = (uint8_t)json_int(j, "buttons", 0);
        usb_hid_mouse(HID_SRC_DEBUG, buttons, json_int(j, "dx", 0), json_int(j, "dy", 0), json_int(j, "wheel", 0),
                      json_int(j, "pan", 0));
        vTaskDelay(pdMS_TO_TICKS(hold));
        usb_hid_mouse(HID_SRC_DEBUG, 0, 0, 0, 0, 0);
    } else if (cJSON_IsString(kind) && strcmp(kind->valuestring, "consumer") == 0) {
        usb_hid_consumer(HID_SRC_DEBUG, (uint16_t)json_int(j, "usage", 0));
        vTaskDelay(pdMS_TO_TICKS(hold));
        usb_hid_consumer(HID_SRC_DEBUG, 0);
    } else if (cJSON_IsString(kind) && strcmp(kind->valuestring, "system") == 0) {
        usb_hid_system(HID_SRC_DEBUG, (uint8_t)json_int(j, "bits", 0));
        vTaskDelay(pdMS_TO_TICKS(hold));
        usb_hid_system(HID_SRC_DEBUG, 0);
    } else {
        ok = false;
    }
    cJSON_Delete(j);
    if (!ok) {
        return send_error(req, "400 Bad Request", "bad_kind", "kind must be key, mouse, consumer or system");
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}
#endif /* CONFIG_HUBBLE_USB_DEBUG_INJECT */

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
        {.uri = "/api/v1/system/wifi-off", .method = HTTP_POST, .handler = wifi_off_post},
        {.uri = "/api/v1/devices", .method = HTTP_GET, .handler = devices_get},
        {.uri = "/api/v1/devices/*", .method = HTTP_GET, .handler = device_item_get},
        {.uri = "/api/v1/devices/*", .method = HTTP_PATCH, .handler = device_item_patch},
        {.uri = "/api/v1/devices/*", .method = HTTP_DELETE, .handler = device_item_delete},
        {.uri = "/api/v1/pairing/scan", .method = HTTP_GET, .handler = pairing_scan_get},
        {.uri = "/api/v1/pairing/scan", .method = HTTP_POST, .handler = pairing_scan_post},
        {.uri = "/api/v1/pairing/start", .method = HTTP_POST, .handler = pairing_start_post},
        {.uri = "/api/v1/pairing/cancel", .method = HTTP_POST, .handler = pairing_cancel_post},
        {.uri = "/api/v1/settings", .method = HTTP_GET, .handler = settings_get},
        {.uri = "/api/v1/settings", .method = HTTP_PUT, .handler = settings_put},
        {.uri = "/api/v1/settings/sta", .method = HTTP_PUT, .handler = settings_sta_put},
        {.uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get},
        {.uri = "/api/v1/ota/check", .method = HTTP_POST, .handler = ota_check_post},
        {.uri = "/api/v1/ota/install", .method = HTTP_POST, .handler = ota_install_post},
        {.uri = "/api/v1/backup", .method = HTTP_GET, .handler = backup_get},
        {.uri = "/api/v1/restore", .method = HTTP_POST, .handler = restore_post},
        {.uri = "/api/v1/system/factory-reset", .method = HTTP_POST, .handler = factory_reset_post},
#if CONFIG_HUBBLE_USB_DEBUG_INJECT
        {.uri = "/api/v1/debug/input", .method = HTTP_POST, .handler = debug_input_post},
#endif
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
