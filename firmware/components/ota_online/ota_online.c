/**
 * @file ota_online.c
 * @brief GitHub update engine. See ota_online.h.
 *
 * Dependencies: ota, ota_manifest, wifi_ap, settings, ble_central, esp_http_client, esp_crt_bundle, mbedtls.
 * Threading: one worker task per operation; s_st is guarded by s_lock; the confirmation flag is volatile.
 */
#include "ota_online.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_central.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "ota.h"
#include "sdkconfig.h"
#include "settings.h"
#include "wifi_ap.h"

static const char *TAG = "ota_online";

/** Repository that publishes the releases. */
#define REPO CONFIG_HUBBLE_UPDATE_REPO
/** Worker stack: TLS handshakes with the certificate bundle need about 8 KB. */
#define WORKER_STACK 10240
/** Worker priority (below the HID path and the Wi-Fi task). */
#define WORKER_PRIO 4
/** Time allowed to join the Wi-Fi network, ms. */
#define STA_TIMEOUT_MS 20000
/** Socket timeout of every HTTP request, ms. */
#define HTTP_TIMEOUT_MS 15000
/** HTTP header buffer: GitHub's redirect to the asset store carries a long signed URL. */
#define HTTP_HEADER_BUF 3072
/** Maximum redirects followed. */
#define MAX_REDIRECTS 5
/** Bytes of the API answer that are read: the newest release's `tag_name` is in the first kilobytes. */
#define API_READ_MAX 4096
/** Largest manifest accepted. */
#define MANIFEST_MAX 6144
/** Download chunk size (heap, freed after the install). */
#define CHUNK 4096
/** Delay before the restart after a successful install, so the UI can show the result. */
#define RESTART_DELAY_MS 3000
/** Poll period while waiting for the physical confirmation. */
#define CONFIRM_POLL_MS 200
/** User agent: GitHub rejects requests without one. Generic on purpose (no personal data). */
#define USER_AGENT "HubBLE-updater"

/** Status copy for readers. */
static ou_status_t s_st;
/** Guards s_st. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/** Release tag found by the last check (the asset URL is built from it). */
static char s_tag[OTA_TAG_MAX];
/** true while a worker task exists. */
static volatile bool s_busy;
/** Set by ota_online_physical_confirm(). */
static volatile bool s_confirmed;
/** Change notification. */
static void (*s_changed)(void);

/** @brief Notify the listener. */
static void notify(void) {
    if (s_changed != NULL) {
        s_changed();
    }
}

/**
 * @brief Update the state (and optionally the error) and notify.
 *
 * @param state New state.
 * @param err   Error code (OUE_NONE unless the state is OU_ERROR).
 */
static void set_state(ou_state_t state, ou_error_t err) {
    portENTER_CRITICAL(&s_lock);
    s_st.state = state;
    s_st.error = err;
    portEXIT_CRITICAL(&s_lock);
    notify();
}

/** @brief Fail the operation: leave the station network, release the AP hold, publish the error. */
static void finish_error(ou_error_t err) {
    ESP_LOGW(TAG, "failed: %s", ota_online_error_name(err));
    wifi_ap_sta_disconnect();
    wifi_ap_set_ota_active(false);
    set_state(OU_ERROR, err);
}

/** @copydoc ota_online_set_changed_cb */
void ota_online_set_changed_cb(void (*cb)(void)) {
    s_changed = cb;
}

/** @copydoc ota_online_get_status */
void ota_online_get_status(ou_status_t *out) {
    portENTER_CRITICAL(&s_lock);
    *out = s_st;
    portEXIT_CRITICAL(&s_lock);
}

/** @copydoc ota_online_state_name */
const char *ota_online_state_name(ou_state_t s) {
    static const char *const names[] = {"idle",    "connecting",  "checking",  "available", "up_to_date",
                                        "confirm", "downloading", "installed", "error"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "idle";
}

/** @copydoc ota_online_error_name */
const char *ota_online_error_name(ou_error_t e) {
    static const char *const names[] = {"none",
                                        "no_wifi_credentials",
                                        "wifi_failed",
                                        "network_error",
                                        "no_release",
                                        "bad_manifest",
                                        "no_board_image",
                                        "unsigned",
                                        "too_old",
                                        "downgrade",
                                        "confirm_timeout",
                                        "hash_mismatch",
                                        "install_failed",
                                        "busy"};
    return (unsigned)e < sizeof(names) / sizeof(names[0]) ? names[e] : "none";
}

/**
 * @brief Open an HTTPS GET, following redirects, and return the client positioned at the body.
 *
 * @param url URL.
 * @return Open client, or NULL on a connection/TLS/HTTP error (final status other than 200).
 */
static esp_http_client_handle_t http_open(const char *url) {
    const esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = HTTP_HEADER_BUF,
        .buffer_size_tx = 1024,
        .user_agent = USER_AGENT,
        .disable_auto_redirect = true, /* Handled below so the body can be streamed from the final URL. */
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == NULL) {
        return NULL;
    }
    esp_http_client_set_header(c, "Accept", "application/vnd.github+json, application/json, application/octet-stream");
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        const esp_err_t open_err = esp_http_client_open(c, 0);
        if (open_err != ESP_OK) {
            /* esp-tls logs the TLS detail just before this line (certificate verification, handshake, DNS). */
            ESP_LOGW(TAG, "cannot open %.60s: %s", url, esp_err_to_name(open_err));
            break;
        }
        esp_http_client_fetch_headers(c);
        const int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            return c;
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (esp_http_client_set_redirection(c) != ESP_OK) {
                break;
            }
            esp_http_client_close(c);
            continue;
        }
        ESP_LOGW(TAG, "HTTP status %d", status);
        break;
    }
    esp_http_client_cleanup(c);
    return NULL;
}

/**
 * @brief GET a small document into a heap buffer (at most @p max bytes are kept, the rest is ignored).
 *
 * @param url Document URL.
 * @param max Maximum bytes to keep.
 * @param[out] len Receives the number of bytes kept.
 * @return NUL-terminated heap buffer (free() it), or NULL on error.
 */
static char *http_get_small(const char *url, size_t max, size_t *len) {
    esp_http_client_handle_t c = http_open(url);
    if (c == NULL) {
        return NULL;
    }
    char *buf = malloc(max + 1);
    size_t n = 0;
    while (buf != NULL && n < max) {
        const int r = esp_http_client_read(c, buf + n, (int)(max - n));
        if (r <= 0) {
            break;
        }
        n += (size_t)r;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (buf == NULL) {
        return NULL;
    }
    buf[n] = '\0';
    *len = n;
    return buf;
}

/**
 * @brief Join the saved Wi-Fi network.
 *
 * @return OUE_NONE on success, or the error to report.
 */
static ou_error_t join_wifi(void) {
    char ssid[SETTINGS_SSID_MAX_LEN + 1];
    char pass[SETTINGS_PASSWORD_MAX_LEN + 1];
    if (settings_get_sta(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        return OUE_NO_STA;
    }
    set_state(OU_CONNECTING, OUE_NONE);
    const esp_err_t err = wifi_ap_sta_connect(ssid, pass, STA_TIMEOUT_MS);
    memset(pass, 0, sizeof(pass));
    return err == ESP_OK ? OUE_NONE : OUE_WIFI;
}

/**
 * @brief Worker: find the newest release, read and evaluate its manifest.
 *
 * @param arg Unused.
 */
static void check_task(void *arg) {
    (void)arg;
    wifi_ap_set_ota_active(true); /* Keeps the AP up while the radio is shared with the station. */
    ou_error_t e = join_wifi();
    if (e != OUE_NONE) {
        finish_error(e);
        s_busy = false;
        vTaskDelete(NULL);
    }
    set_state(OU_CHECKING, OUE_NONE);

    char url[200];
    const bool pre = s_st.prerelease_channel;
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases%s", REPO, pre ? "?per_page=1" : "/latest");
    size_t len = 0;
    char *api = http_get_small(url, API_READ_MAX, &len);
    char tag[OTA_TAG_MAX];
    if (api == NULL) {
        finish_error(OUE_NETWORK);
        s_busy = false;
        vTaskDelete(NULL);
    }
    const bool found = ota_manifest_find_tag(api, len, tag, sizeof(tag));
    free(api);
    if (!found) {
        finish_error(OUE_NO_RELEASE);
        s_busy = false;
        vTaskDelete(NULL);
    }

    snprintf(url, sizeof(url), "https://github.com/%s/releases/download/%s/manifest.json", REPO, tag);
    char *mf = http_get_small(url, MANIFEST_MAX, &len);
    if (mf == NULL) {
        finish_error(OUE_BAD_MANIFEST);
        s_busy = false;
        vTaskDelete(NULL);
    }
    ota_release_t rel;
    const ota_manifest_result_t pr = ota_manifest_parse(mf, board_get_info()->id, &rel);
    free(mf);
    if (pr != OTA_MANIFEST_OK) {
        finish_error(pr == OTA_MANIFEST_NO_BOARD ? OUE_NO_BOARD : OUE_BAD_MANIFEST);
        s_busy = false;
        vTaskDelete(NULL);
    }

    const ota_decision_t d = ota_manifest_decide(esp_app_get_description()->version, &rel, true);
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_tag, tag, sizeof(s_tag));
    s_st.release = rel;
    s_st.decision = d;
    s_st.have_release = true;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "latest %s (decision %d)", rel.version, (int)d);

    wifi_ap_sta_disconnect();
    wifi_ap_set_ota_active(false);
    switch (d) {
    case OTA_DECISION_INSTALL:
    case OTA_DECISION_DOWNGRADE:
        set_state(OU_AVAILABLE, OUE_NONE);
        break;
    case OTA_DECISION_UP_TO_DATE:
        set_state(OU_UP_TO_DATE, OUE_NONE);
        break;
    case OTA_DECISION_TOO_OLD:
        set_state(OU_ERROR, OUE_TOO_OLD);
        break;
    case OTA_DECISION_UNSIGNED:
        set_state(OU_ERROR, OUE_UNSIGNED);
        break;
    case OTA_DECISION_BAD_VERSION:
    default:
        set_state(OU_ERROR, OUE_BAD_MANIFEST);
        break;
    }
    s_busy = false;
    vTaskDelete(NULL);
}

/** @copydoc ota_online_check */
esp_err_t ota_online_check(bool prerelease) {
    if (s_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!settings_has_sta()) {
        return ESP_ERR_NOT_FOUND;
    }
    wifi_ap_status_t ap;
    wifi_ap_get_status(&ap);
    if (!ap.on) {
        return ESP_ERR_INVALID_STATE;
    }
    s_busy = true;
    portENTER_CRITICAL(&s_lock);
    memset(&s_st, 0, sizeof(s_st));
    s_st.prerelease_channel = prerelease;
    portEXIT_CRITICAL(&s_lock);
    if (xTaskCreate(check_task, "ota_check", WORKER_STACK, NULL, WORKER_PRIO, NULL) != pdPASS) {
        s_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/** @brief Hex-encode a SHA-256 digest (lower case). */
static void to_hex(const uint8_t d[32], char out[65]) {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 0xF];
    }
    out[64] = '\0';
}

/**
 * @brief Worker: confirm (if required), download, verify and install.
 *
 * @param arg Unused.
 */
static void install_task(void *arg) {
    (void)arg;
    ota_release_t rel;
    char tag[OTA_TAG_MAX];
    portENTER_CRITICAL(&s_lock);
    rel = s_st.release;
    strlcpy(tag, s_tag, sizeof(tag));
    portEXIT_CRITICAL(&s_lock);

    wifi_ap_set_ota_active(true);
    ou_error_t err = OUE_NONE;
    uint8_t *buf = NULL;
    esp_http_client_handle_t c = NULL;
    bool session = false;

    /* Physical confirmation (CLAUDE.md §3.4.3): needs an already paired keyboard, otherwise skipped. */
    if (settings_get_require_confirm() && ble_central_has_keyboard()) {
        s_confirmed = false;
        const uint32_t deadline = CONFIG_HUBBLE_UPDATE_CONFIRM_TIMEOUT_S * 1000u;
        for (uint32_t waited = 0; !s_confirmed; waited += CONFIRM_POLL_MS) {
            portENTER_CRITICAL(&s_lock);
            s_st.confirm_remaining_s = (deadline - (waited < deadline ? waited : deadline) + 999) / 1000;
            portEXIT_CRITICAL(&s_lock);
            if (waited == 0 || waited % 1000 == 0) {
                set_state(OU_AWAIT_CONFIRM, OUE_NONE);
            }
            if (waited >= deadline) {
                err = OUE_CONFIRM_TIMEOUT;
                goto done;
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIRM_POLL_MS));
        }
    }

    err = join_wifi();
    if (err != OUE_NONE) {
        goto done;
    }
    char url[256];
    snprintf(url, sizeof(url), "https://github.com/%s/releases/download/%s/%s", REPO, tag, rel.file);
    c = http_open(url);
    buf = malloc(CHUNK);
    if (c == NULL || buf == NULL) {
        err = OUE_NETWORK;
        goto done;
    }
    if (ota_session_begin(rel.size) != OTA_OK) {
        err = OUE_INSTALL;
        goto done;
    }
    session = true;
    portENTER_CRITICAL(&s_lock);
    s_st.total = rel.size;
    s_st.received = 0;
    portEXIT_CRITICAL(&s_lock);
    set_state(OU_DOWNLOADING, OUE_NONE);

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint32_t got = 0;
    uint32_t last_pct = 0;
    for (;;) {
        const int r = esp_http_client_read(c, (char *)buf, CHUNK);
        if (r < 0) {
            err = OUE_NETWORK;
            break;
        }
        if (r == 0) {
            break; /* End of body (or timeout; the size check below tells them apart). */
        }
        got += (uint32_t)r;
        if (got > rel.size) {
            err = OUE_HASH; /* More data than the manifest promised. */
            break;
        }
        mbedtls_sha256_update(&sha, buf, (size_t)r);
        if (ota_session_write(buf, (size_t)r) != OTA_OK) {
            session = false; /* The session aborts itself on a write error. */
            err = OUE_INSTALL;
            break;
        }
        const uint32_t pct = got * 100u / rel.size;
        if (pct != last_pct) {
            last_pct = pct;
            portENTER_CRITICAL(&s_lock);
            s_st.received = got;
            portEXIT_CRITICAL(&s_lock);
            notify();
        }
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (err == OUE_NONE && got != rel.size) {
        err = OUE_NETWORK;
    }
    if (err == OUE_NONE) {
        char hex[65];
        to_hex(digest, hex);
        if (strcmp(hex, rel.sha256) != 0) {
            err = OUE_HASH; /* Checked BEFORE the slot is selected: a bad download never becomes bootable. */
        }
    }
    if (err == OUE_NONE) {
        char version[OTA_VERSION_MAX];
        /* esp_ota_end() inside also verifies the RSA signature block (signed app images, ADR 6). */
        session = false;
        if (ota_session_finish(version, sizeof(version)) != OTA_OK) {
            err = OUE_INSTALL;
        }
    }

done:
    if (session) {
        ota_session_abort();
    }
    if (c != NULL) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    free(buf);
    if (err != OUE_NONE) {
        finish_error(err);
    } else {
        wifi_ap_sta_disconnect();
        set_state(OU_INSTALLED, OUE_NONE);
        ESP_LOGI(TAG, "installed %s, restarting", rel.version);
        vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
        esp_restart();
    }
    s_busy = false;
    vTaskDelete(NULL);
}

/** @copydoc ota_online_install */
esp_err_t ota_online_install(bool allow_downgrade) {
    ou_status_t st;
    ota_online_get_status(&st);
    if (s_busy || st.state != OU_AVAILABLE || !st.have_release) {
        return ESP_ERR_INVALID_STATE;
    }
    if (st.decision == OTA_DECISION_DOWNGRADE && !allow_downgrade) {
        return ESP_ERR_NOT_ALLOWED;
    }
    s_busy = true;
    if (xTaskCreate(install_task, "ota_install", WORKER_STACK, NULL, WORKER_PRIO, NULL) != pdPASS) {
        s_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/** @copydoc ota_online_physical_confirm */
void ota_online_physical_confirm(void) {
    if (s_st.state == OU_AWAIT_CONFIRM) {
        s_confirmed = true;
    }
}
