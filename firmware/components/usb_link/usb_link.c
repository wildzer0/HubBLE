/**
 * @file usb_link.c
 * @brief USB configuration link (see usb_link.h and ulink_proto.h).
 *
 * Dependencies: see usb_link.h. Threading: everything protocol-related runs in the "usb_link" task.
 */
#include "usb_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "ulink_proto.h"
#include "usb_hid.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "usb_link";

/** Stack and priority of the link task (below the HID report task, above the idle work). */
#define LINK_TASK_STACK 7168
#define LINK_TASK_PRIO  5
/** Reports buffered between the TinyUSB callback and the link task (8 reports). */
#define RX_BUFFER_BYTES (8 * (ULINK_REPORT_SIZE + 4))
/** How long the task waits for a report before looking at the event queue, ms. */
#define RX_WAIT_MS 20
/** Events waiting to be sent, and the longest event JSON kept. */
#define EVENT_QUEUE_LEN 12
#define EVENT_MAX       700
/** The host counts as present for this long after its last frame, ms (the web app sends a heartbeat every 5 s). */
#define HOST_TTL_MS 15000
/** Loopback target: the dongle's own HTTP server. */
#define LOOPBACK_PORT 80
/** The Host header the API requires (http_guard). */
#define API_HOST "192.168.4.1"
/** Longest request path accepted. */
#define PATH_MAX_LEN 200
/** Socket timeouts, seconds: a firmware upload finishes with a flash write and a reboot request. */
#define SOCK_TIMEOUT_S 60
/** How long a frame may wait for room in the transmit queue, ms. */
#define TX_WAIT_MS 500

/** State of the request being served. */
typedef enum { REQ_NONE = 0, REQ_BODY, REQ_REJECT } req_state_t;

/** The request being served (one at a time: the web app waits for each response). */
static struct {
    req_state_t state; /**< Where we are. */
    int id;            /**< Request id echoed in the response. */
    int sock;          /**< Loopback socket, or -1. */
    size_t remaining;  /**< Body bytes still expected. */
} s_req = {.sock = -1};

static MessageBufferHandle_t s_rx;       /**< Reports from the TinyUSB task. */
static QueueHandle_t s_events;           /**< Event JSON strings (malloc'd) from the event sink. */
static ulink_parser_t s_parser;          /**< Frame parser (link task only). */
static volatile int64_t s_last_host_ms;  /**< Time of the last frame from the host. */
static uint8_t s_out[ULINK_REPORT_DATA]; /**< Stream bytes waiting to fill a report. */
static size_t s_out_n;                   /**< Bytes in s_out. */
static bool s_tx_failed;                 /**< A report could not be queued: the host is gone. */

/** @brief Milliseconds since boot. */
static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

/* ---- Sending ------------------------------------------------------------------------------------ */

/** @brief Send the pending stream bytes as one report. */
static void flush_report(void) {
    if (s_out_n == 0) {
        return;
    }
    uint8_t rep[ULINK_REPORT_SIZE];
    ulink_pack_report(rep, s_out, s_out_n);
    s_out_n = 0;
    if (!usb_hid_link_send(rep, TX_WAIT_MS)) {
        s_tx_failed = true;
    }
}

/** @brief Append stream bytes, sending a report whenever one is full. */
static void stream_write(const uint8_t *data, size_t n) {
    while (n > 0 && !s_tx_failed) {
        size_t take = ULINK_REPORT_DATA - s_out_n;
        if (take > n) {
            take = n;
        }
        memcpy(&s_out[s_out_n], data, take);
        s_out_n += take;
        data += take;
        n -= take;
        if (s_out_n == ULINK_REPORT_DATA) {
            flush_report();
        }
    }
}

/**
 * @brief Send one frame.
 *
 * @param type    Frame type.
 * @param payload Payload (may be NULL if @p len is 0).
 * @param len     Payload length (<= ULINK_MAX_PAYLOAD).
 * @return false if the host stopped taking reports.
 */
static bool send_frame(uint8_t type, const void *payload, size_t len) {
    uint8_t h[3];
    s_tx_failed = false;
    ulink_encode_header(type, len, h);
    stream_write(h, sizeof(h));
    stream_write(payload, len);
    flush_report();
    return !s_tx_failed;
}

/** @brief Send a JSON text frame. */
static bool send_json(uint8_t type, const char *json) {
    return send_frame(type, json, strlen(json));
}

/** @brief Send an ERROR frame. */
static void send_error(const char *code, const char *message) {
    char j[160];
    snprintf(j, sizeof(j), "{\"code\":\"%s\",\"message\":\"%s\"}", code, message);
    send_json(ULINK_ERROR, j);
}

/** @brief Answer a request that is refused before it reaches the API. */
static void send_refusal(int id, int status, const char *code, const char *message) {
    char head[96];
    char body[160];
    snprintf(head, sizeof(head), "{\"id\":%d,\"status\":%d,\"ct\":\"application/json\"}", id, status);
    snprintf(body, sizeof(body), "{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}", code, message);
    send_json(ULINK_RES_HEAD, head);
    send_frame(ULINK_RES_BODY, body, strlen(body));
    send_frame(ULINK_RES_END, NULL, 0);
}

/* ---- The loopback request ------------------------------------------------------------------------ */

/** @brief Close the request socket. */
static void req_close(void) {
    if (s_req.sock >= 0) {
        close(s_req.sock);
        s_req.sock = -1;
    }
    s_req.state = REQ_NONE;
    s_req.remaining = 0;
}

/** @brief Send all of a buffer on the request socket. */
static bool sock_send_all(const uint8_t *data, size_t n) {
    while (n > 0) {
        const int w = send(s_req.sock, data, n, 0);
        if (w <= 0) {
            return false;
        }
        data += w;
        n -= (size_t)w;
    }
    return true;
}

/**
 * @brief Read the response from the socket and stream it to the host.
 *
 * Called when the request has been sent completely. Decodes a chunked body, so the host always gets plain bytes.
 */
static void relay_response(void) {
    static uint8_t buf[1024];
    static uint8_t dec[1024];
    size_t have = 0;
    ulink_http_head_t head;
    int rc = 0;
    while (rc == 0) {
        const int r = recv(s_req.sock, buf + have, sizeof(buf) - have, 0);
        if (r <= 0) {
            break;
        }
        have += (size_t)r;
        rc = ulink_http_parse_head(buf, have, &head);
    }
    if (rc != 1) {
        send_refusal(s_req.id, 502, "bad_gateway", "No valid answer from the dongle's API");
        req_close();
        return;
    }
    char hj[160];
    snprintf(hj, sizeof(hj), "{\"id\":%d,\"status\":%d,\"ct\":\"%s\"}", s_req.id, head.status,
             head.content_type[0] ? head.content_type : "application/json");
    if (!send_json(ULINK_RES_HEAD, hj)) {
        req_close();
        return;
    }
    ulink_chunked_t ch;
    ulink_chunked_init(&ch);
    bool done = false;
    size_t off = head.head_len;
    long sent_total = 0;
    for (;;) {
        const uint8_t *in = buf + off;
        const size_t n = have - off;
        if (n > 0) {
            if (head.chunked) {
                const long m = ulink_chunked_feed(&ch, in, n, dec, &done);
                if (m < 0) {
                    break;
                }
                if (m > 0 && !send_frame(ULINK_RES_BODY, dec, (size_t)m)) {
                    req_close();
                    return;
                }
            } else if (!send_frame(ULINK_RES_BODY, in, n)) {
                req_close();
                return;
            }
            sent_total += (long)n;
        }
        if (done || (!head.chunked && head.content_length >= 0 && sent_total >= head.content_length)) {
            break;
        }
        const int r = recv(s_req.sock, buf, sizeof(buf), 0);
        if (r <= 0) {
            break; /* Connection: close ends a body without a length. */
        }
        have = (size_t)r;
        off = 0;
    }
    send_frame(ULINK_RES_END, NULL, 0);
    req_close();
}

/** @brief Whether a method is one the API uses. */
static bool method_ok(const char *m) {
    return strcmp(m, "GET") == 0 || strcmp(m, "POST") == 0 || strcmp(m, "PUT") == 0 || strcmp(m, "PATCH") == 0 ||
           strcmp(m, "DELETE") == 0;
}

/** @brief Whether a path is a plain API path without anything that could break the request line. */
static bool path_ok(const char *p) {
    if (strncmp(p, "/api/v1/", 8) != 0 || strlen(p) > PATH_MAX_LEN || strcmp(p, "/api/v1/ws") == 0) {
        return false;
    }
    for (; *p != '\0'; p++) {
        if ((unsigned char)*p <= ' ' || *p == 0x7F) {
            return false;
        }
    }
    return true;
}

/**
 * @brief After a refusal: swallow the request body (the host still sends it, one acknowledged chunk at a time).
 *
 * @param body_len Body bytes the host announced.
 */
static void reject_body(size_t body_len) {
    if (s_req.sock >= 0) {
        close(s_req.sock);
        s_req.sock = -1;
    }
    s_req.state = body_len > 0 ? REQ_REJECT : REQ_NONE;
    s_req.remaining = body_len;
    if (body_len > 0) {
        send_frame(ULINK_ACK, NULL, 0);
    }
}

/** @brief Handle REQ_HEAD: open the loopback connection and send the request head. */
static void on_req_head(const uint8_t *payload, size_t len) {
    req_close();
    char text[ULINK_MAX_PAYLOAD + 1];
    memcpy(text, payload, len);
    text[len] = '\0';
    cJSON *j = cJSON_Parse(text);
    const cJSON *id = j ? cJSON_GetObjectItemCaseSensitive(j, "id") : NULL;
    const cJSON *method = j ? cJSON_GetObjectItemCaseSensitive(j, "method") : NULL;
    const cJSON *path = j ? cJSON_GetObjectItemCaseSensitive(j, "path") : NULL;
    const cJSON *ct = j ? cJSON_GetObjectItemCaseSensitive(j, "ct") : NULL;
    const cJSON *blen = j ? cJSON_GetObjectItemCaseSensitive(j, "len") : NULL;
    s_req.id = cJSON_IsNumber(id) ? id->valueint : 0;
    const size_t body_len = cJSON_IsNumber(blen) && blen->valueint > 0 ? (size_t)blen->valueint : 0;
    if (!cJSON_IsString(method) || !cJSON_IsString(path) || !method_ok(method->valuestring) ||
        !path_ok(path->valuestring)) {
        send_refusal(s_req.id, 400, "bad_request", "Unsupported method or path");
        reject_body(body_len);
        cJSON_Delete(j);
        return;
    }
    if (!wifi_ap_config_open()) {
        send_refusal(s_req.id, 403, "usb_locked",
                     "The configuration window is closed: press BOOT on the dongle or replug it");
        reject_body(body_len);
        cJSON_Delete(j);
        return;
    }
    wifi_ap_note_usb_activity();

    s_req.sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(LOOPBACK_PORT)};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    struct timeval tv = {.tv_sec = SOCK_TIMEOUT_S};
    if (s_req.sock >= 0) {
        setsockopt(s_req.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s_req.sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    if (s_req.sock < 0 || connect(s_req.sock, (struct sockaddr *)&a, sizeof(a)) != 0) {
        send_refusal(s_req.id, 503, "api_unavailable", "The dongle's API is not reachable");
        reject_body(body_len);
        cJSON_Delete(j);
        return;
    }
    /* The API requires a custom header and, for changes, a JSON or octet-stream content type (CSRF guard). */
    char head[420];
    int n = snprintf(head, sizeof(head),
                     "%s %s HTTP/1.1\r\nHost: " API_HOST "\r\nX-HubBLE: 1\r\nAccept: */*\r\nConnection: close\r\n",
                     method->valuestring, path->valuestring);
    if (strcmp(method->valuestring, "GET") != 0 && strcmp(method->valuestring, "DELETE") != 0) {
        const char *type = cJSON_IsString(ct) && strlen(ct->valuestring) < 60 ? ct->valuestring : "application/json";
        n += snprintf(head + n, sizeof(head) - (size_t)n, "Content-Type: %s\r\nContent-Length: %u\r\n", type,
                      (unsigned)body_len);
    }
    if (n > 0 && n < (int)sizeof(head) - 3) {
        n += snprintf(head + n, sizeof(head) - (size_t)n, "\r\n");
    }
    cJSON_Delete(j);
    if (n <= 0 || n >= (int)sizeof(head) || !sock_send_all((const uint8_t *)head, (size_t)n)) {
        send_refusal(s_req.id, 502, "bad_gateway", "Could not reach the API");
        reject_body(body_len);
        return;
    }
    if (body_len > 0) {
        s_req.state = REQ_BODY;
        s_req.remaining = body_len;
        send_frame(ULINK_ACK, NULL, 0); /* The host may send the first chunk. */
    } else {
        relay_response();
    }
}

/** @brief Handle REQ_BODY: forward the chunk, acknowledge it, answer when the body is complete. */
static void on_req_body(const uint8_t *payload, size_t len) {
    if (s_req.state == REQ_NONE) {
        send_error("no_request", "Body without a request");
        return;
    }
    if (len > s_req.remaining) {
        len = s_req.remaining;
    }
    if (s_req.state == REQ_BODY && !sock_send_all(payload, len)) {
        send_refusal(s_req.id, 502, "bad_gateway", "The API stopped reading");
        s_req.state = REQ_REJECT; /* Swallow the rest of the body. */
        if (s_req.sock >= 0) {
            close(s_req.sock);
            s_req.sock = -1;
        }
    }
    s_req.remaining -= len;
    if (s_req.remaining > 0) {
        send_frame(ULINK_ACK, NULL, 0);
        return;
    }
    if (s_req.state == REQ_BODY) {
        relay_response();
    } else {
        req_close();
        send_frame(ULINK_ACK, NULL, 0);
    }
}

/* ---- Frames from the host ------------------------------------------------------------------------ */

/** @brief Handle one frame from the host. */
static void on_frame(uint8_t type, const uint8_t *payload, size_t len, void *ctx) {
    (void)ctx;
    s_last_host_ms = now_ms();
    switch (type) {
    case ULINK_HELLO: {
        char j[200];
        snprintf(j, sizeof(j), "{\"proto\":%d,\"fw\":\"%s\",\"board\":\"%s\",\"open\":%s}", ULINK_PROTO,
                 esp_app_get_description()->version, board_get_info()->id, wifi_ap_config_open() ? "true" : "false");
        send_json(ULINK_INFO, j);
        break;
    }
    case ULINK_LIVE: {
        if (len > 0 && len < 200 && wifi_ap_config_open()) {
            char text[200];
            memcpy(text, payload, len);
            text[len] = '\0';
            web_server_usb_message(text);
            wifi_ap_note_usb_activity();
        }
        break;
    }
    case ULINK_REQ_HEAD:
        on_req_head(payload, len);
        break;
    case ULINK_REQ_BODY:
        on_req_body(payload, len);
        break;
    default:
        break; /* Unknown types are ignored so newer web apps can talk to older firmware. */
    }
}

/* ---- Events to the host ------------------------------------------------------------------------- */

/** @brief Whether the host is listening (it sent something recently). */
static bool host_present(void) {
    return s_last_host_ms != 0 && now_ms() - s_last_host_ms < HOST_TTL_MS;
}

/**
 * @brief Event sink installed in the web server: queue the event for the link task. Never blocks.
 *
 * @param event     Event name.
 * @param data_json JSON for `data`.
 */
static void event_sink(const char *event, const char *data_json) {
    if (s_events == NULL || !host_present() || !wifi_ap_config_open()) {
        return;
    }
    const size_t cap = strlen(event) + strlen(data_json) + 32;
    if (cap > EVENT_MAX) {
        return; /* Too big for the link: the web app polls for the full state anyway. */
    }
    char *m = malloc(cap);
    if (m == NULL) {
        return;
    }
    snprintf(m, cap, "{\"event\":\"%s\",\"data\":%s}", event, data_json);
    if (xQueueSend(s_events, &m, 0) != pdTRUE) {
        free(m);
    }
}

/** @brief Whether the web server should produce periodic events for the link. */
static bool events_wanted(void) {
    return host_present() && wifi_ap_config_open();
}

/** @brief Send the queued events (only between requests). */
static void drain_events(void) {
    char *m;
    while (s_req.state == REQ_NONE && xQueueReceive(s_events, &m, 0) == pdTRUE) {
        if (host_present()) {
            send_json(ULINK_EVENT, m);
        }
        free(m);
    }
}

/* ---- Task ---------------------------------------------------------------------------------------- */

/** @brief Receive hook called by usb_hid in the TinyUSB task for every OUT report. */
static void rx_report(const uint8_t *report, size_t len) {
    if (s_rx != NULL) {
        xMessageBufferSend(s_rx, report, len, 0); /* Full: drop. The host's window of one keeps this rare. */
    }
}

/** @brief The link task. */
static void link_task(void *arg) {
    (void)arg;
    uint8_t rep[ULINK_REPORT_SIZE];
    for (;;) {
        const size_t n = xMessageBufferReceive(s_rx, rep, sizeof(rep), pdMS_TO_TICKS(RX_WAIT_MS));
        if (n > 0) {
            ulink_parser_feed_report(&s_parser, rep, n, on_frame, NULL);
        }
        drain_events();
    }
}

/** @copydoc usb_link_start */
esp_err_t usb_link_start(void) {
    s_rx = xMessageBufferCreate(RX_BUFFER_BYTES);
    s_events = xQueueCreate(EVENT_QUEUE_LEN, sizeof(char *));
    if (s_rx == NULL || s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ulink_parser_init(&s_parser);
    usb_hid_link_set_rx(rx_report);
    web_server_set_event_sink(event_sink, events_wanted);
    if (xTaskCreate(link_task, "usb_link", LINK_TASK_STACK, NULL, LINK_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "configuration link ready");
    return ESP_OK;
}
