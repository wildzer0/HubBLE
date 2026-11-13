/**
 * @file settings.c
 * @brief NVS-backed settings. See settings.h.
 *
 * Dependencies: nvs_flash. Threading: NVS handle is read-only after init; NVS calls are thread-safe.
 */
#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

/** NVS namespace for all HubBLE settings. */
#define NS "hubble"
/** NVS key names (max 15 chars). */
#define KEY_AP_PASS     "ap_pass"
#define KEY_REQ_CONFIRM "req_conf"
#define KEY_STA_SSID    "sta_ssid"
#define KEY_STA_PASS    "sta_pass"
#define KEY_CHANNEL     "upd_pre"
#define KEY_REM_WAKE    "rem_wake"
#define KEY_PORTAL_AUTO "portal_auto"
#define KEY_USB_LINK    "usb_link"
#define KEY_FLT_ON      "flt_on"
#define KEY_FLT_BOUNCE  "flt_bounce"
#define KEY_FLT_HOLD    "flt_hold"
#define KEY_LED_BRIGHT  "led_pct"
#define KEY_STATUS_LED  "led_status"
#define KEY_SHIFT_HOLD  "shift_ms"
#define KEY_BOOT_WIN    "boot_win_s"
#define KEY_IDLE_TMO    "idle_tmo_s"
#define KEY_BOOT_FAIL   "boot_fail"
#define KEY_OTA_RESULT  "ota_result"

/** Open namespace handle; 0 until settings_init() succeeds. */
static nvs_handle_t s_nvs;
/** true once the namespace is open. */
static bool s_ready;

/** @copydoc settings_init */
esp_err_t settings_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* The partition layout/version changed (e.g. after an OTA to a newer IDF): start clean. */
        ESP_LOGW(TAG, "NVS needs erasing (%s)", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    s_ready = (err == ESP_OK);
    return err;
}

/** @copydoc settings_is_ready */
bool settings_is_ready(void) {
    return s_ready;
}

/** @copydoc settings_get_ap_password */
esp_err_t settings_get_ap_password(char *out, size_t cap) {
    if (cap < SETTINGS_PASSWORD_MAX_LEN + 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t len = cap;
    if (s_ready && nvs_get_str(s_nvs, KEY_AP_PASS, out, &len) == ESP_OK) {
        return ESP_OK;
    }
    strcpy(out, SETTINGS_DEFAULT_AP_PASSWORD);
    return ESP_OK;
}

/** @copydoc settings_set_ap_password */
esp_err_t settings_set_ap_password(const char *password) {
    if (!settings_password_valid(password)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_set_str(s_nvs, KEY_AP_PASS, password);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

/** @copydoc settings_reset_ap_password */
esp_err_t settings_reset_ap_password(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_erase_key(s_nvs, KEY_AP_PASS);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; /* Already at the default. */
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

/** @copydoc settings_get_require_confirm */
bool settings_get_require_confirm(void) {
    uint8_t v = 1; /* Default ON (CLAUDE.md §3.4.3). */
    if (s_ready && nvs_get_u8(s_nvs, KEY_REQ_CONFIRM, &v) != ESP_OK) {
        v = 1;
    }
    return v != 0;
}

/** @copydoc settings_set_require_confirm */
esp_err_t settings_set_require_confirm(bool enabled) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u8(s_nvs, KEY_REQ_CONFIRM, enabled ? 1 : 0);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_is_setup_done */
bool settings_is_setup_done(void) {
    size_t len = 0;
    return s_ready && nvs_get_str(s_nvs, KEY_AP_PASS, NULL, &len) == ESP_OK;
}

/** @brief Read a u32 or return a default (helper). */
static uint32_t get_u32(const char *key, uint32_t def) {
    uint32_t v = def;
    if (s_ready && nvs_get_u32(s_nvs, key, &v) != ESP_OK) {
        v = def;
    }
    return v;
}

/** @copydoc settings_get_boot_window_s */
uint32_t settings_get_boot_window_s(void) {
    return get_u32(KEY_BOOT_WIN, SETTINGS_DEFAULT_BOOT_WINDOW_S);
}

/** @copydoc settings_get_idle_timeout_s */
uint32_t settings_get_idle_timeout_s(void) {
    return get_u32(KEY_IDLE_TMO, SETTINGS_DEFAULT_IDLE_TIMEOUT_S);
}

/** @copydoc settings_window_valid */
bool settings_window_valid(uint32_t seconds) {
    return seconds >= SETTINGS_WINDOW_MIN_S && seconds <= SETTINGS_WINDOW_MAX_S;
}

/** @brief Validate and store a u32 setting (helper). */
static esp_err_t set_window(const char *key, uint32_t seconds) {
    if (!settings_window_valid(seconds)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u32(s_nvs, key, seconds);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_set_boot_window_s */
esp_err_t settings_set_boot_window_s(uint32_t seconds) {
    return set_window(KEY_BOOT_WIN, seconds);
}

/** @copydoc settings_set_idle_timeout_s */
esp_err_t settings_set_idle_timeout_s(uint32_t seconds) {
    return set_window(KEY_IDLE_TMO, seconds);
}

/** @copydoc settings_has_sta */
bool settings_has_sta(void) {
    size_t len = 0;
    return s_ready && nvs_get_str(s_nvs, KEY_STA_SSID, NULL, &len) == ESP_OK && len > 1;
}

/** @copydoc settings_get_sta */
esp_err_t settings_get_sta(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap) {
    if (!settings_has_sta()) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t sl = ssid_cap;
    size_t pl = pass_cap;
    pass[0] = '\0';
    esp_err_t err = nvs_get_str(s_nvs, KEY_STA_SSID, ssid, &sl);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(s_nvs, KEY_STA_PASS, pass, &pl);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err; /* No password = open network. */
}

/** @copydoc settings_set_sta */
esp_err_t settings_set_sta(const char *ssid, const char *password) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid != NULL && ssid[0] == '\0') {
        nvs_erase_key(s_nvs, KEY_STA_SSID);
        nvs_erase_key(s_nvs, KEY_STA_PASS);
        return nvs_commit(s_nvs);
    }
    if (!settings_sta_valid(ssid, password)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_set_str(s_nvs, KEY_STA_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(s_nvs, KEY_STA_PASS, password);
    }
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_get_prerelease_channel */
bool settings_get_prerelease_channel(void) {
    uint8_t v = 0;
    return s_ready && nvs_get_u8(s_nvs, KEY_CHANNEL, &v) == ESP_OK && v != 0;
}

/** @copydoc settings_set_prerelease_channel */
esp_err_t settings_set_prerelease_channel(bool prerelease) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u8(s_nvs, KEY_CHANNEL, prerelease ? 1 : 0);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @brief Read a boolean setting stored as u8. */
static bool get_flag(const char *key, bool def) {
    uint8_t v = def ? 1 : 0;
    if (s_ready && nvs_get_u8(s_nvs, key, &v) != ESP_OK) {
        v = def ? 1 : 0;
    }
    return v != 0;
}

/** @brief Store a boolean setting as u8. */
static esp_err_t set_flag(const char *key, bool value) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u8(s_nvs, key, value ? 1 : 0);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_get_remote_wakeup */
bool settings_get_remote_wakeup(void) {
    return get_flag(KEY_REM_WAKE, true);
}

/** @copydoc settings_set_remote_wakeup */
esp_err_t settings_set_remote_wakeup(bool enabled) {
    return set_flag(KEY_REM_WAKE, enabled);
}

/** @copydoc settings_get_portal_auto */
bool settings_get_portal_auto(void) {
    return get_flag(KEY_PORTAL_AUTO, true);
}

/** @copydoc settings_set_portal_auto */
esp_err_t settings_set_portal_auto(bool enabled) {
    return set_flag(KEY_PORTAL_AUTO, enabled);
}

/** @copydoc settings_get_status_led */
bool settings_get_status_led(void) {
    return get_flag(KEY_STATUS_LED, false);
}

/** @copydoc settings_set_status_led */
esp_err_t settings_set_status_led(bool enabled) {
    return set_flag(KEY_STATUS_LED, enabled);
}

/** @copydoc settings_get_led_brightness */
uint8_t settings_get_led_brightness(void) {
    return (uint8_t)get_u32(KEY_LED_BRIGHT, SETTINGS_LED_BRIGHTNESS_DEFAULT);
}

/** @copydoc settings_set_led_brightness */
esp_err_t settings_set_led_brightness(uint32_t percent) {
    if (percent > SETTINGS_LED_BRIGHTNESS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u32(s_nvs, KEY_LED_BRIGHT, percent);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_get_usb_link */
bool settings_get_usb_link(void) {
    return get_flag(KEY_USB_LINK, false);
}

/** @copydoc settings_set_usb_link */
esp_err_t settings_set_usb_link(bool enabled) {
    return set_flag(KEY_USB_LINK, enabled);
}

/** @copydoc settings_get_filter_enabled */
bool settings_get_filter_enabled(void) {
    return get_flag(KEY_FLT_ON, false);
}

/** @copydoc settings_set_filter_enabled */
esp_err_t settings_set_filter_enabled(bool enabled) {
    return set_flag(KEY_FLT_ON, enabled);
}

/** @copydoc settings_get_filter_bounce_ms */
uint32_t settings_get_filter_bounce_ms(void) {
    return get_u32(KEY_FLT_BOUNCE, SETTINGS_FILTER_BOUNCE_DEFAULT_MS);
}

/** @copydoc settings_set_filter_bounce_ms */
esp_err_t settings_set_filter_bounce_ms(uint32_t ms) {
    if (ms > SETTINGS_FILTER_BOUNCE_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u32(s_nvs, KEY_FLT_BOUNCE, ms);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_get_filter_hold_ms */
uint32_t settings_get_filter_hold_ms(void) {
    return get_u32(KEY_FLT_HOLD, 0);
}

/** @copydoc settings_set_filter_hold_ms */
esp_err_t settings_set_filter_hold_ms(uint32_t ms) {
    if (ms > SETTINGS_FILTER_HOLD_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u32(s_nvs, KEY_FLT_HOLD, ms);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_get_shift_hold_ms */
uint32_t settings_get_shift_hold_ms(void) {
    return get_u32(KEY_SHIFT_HOLD, SETTINGS_SHIFT_HOLD_DEFAULT_MS);
}

/** @copydoc settings_set_shift_hold_ms */
esp_err_t settings_set_shift_hold_ms(uint32_t ms) {
    if (ms < SETTINGS_SHIFT_HOLD_MIN_MS || ms > SETTINGS_SHIFT_HOLD_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u32(s_nvs, KEY_SHIFT_HOLD, ms);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

/** @copydoc settings_boot_failures */
uint32_t settings_boot_failures(void) {
    return get_u32(KEY_BOOT_FAIL, 0);
}

/** @copydoc settings_boot_failures_increment */
uint32_t settings_boot_failures_increment(void) {
    const uint32_t n = settings_boot_failures() + 1;
    if (s_ready && nvs_set_u32(s_nvs, KEY_BOOT_FAIL, n) == ESP_OK) {
        nvs_commit(s_nvs);
    }
    return n;
}

/** @copydoc settings_boot_failures_reset */
void settings_boot_failures_reset(void) {
    if (s_ready && nvs_set_u32(s_nvs, KEY_BOOT_FAIL, 0) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

/** @copydoc settings_set_ota_result */
void settings_set_ota_result(const char *text) {
    char buf[SETTINGS_OTA_RESULT_MAX];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (s_ready && nvs_set_str(s_nvs, KEY_OTA_RESULT, buf) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

/** @copydoc settings_get_ota_result */
void settings_get_ota_result(char *out, size_t cap) {
    size_t len = cap;
    if (cap == 0) {
        return;
    }
    if (!s_ready || nvs_get_str(s_nvs, KEY_OTA_RESULT, out, &len) != ESP_OK) {
        out[0] = '\0';
    }
}
