/**
 * @file wifi_ap.c
 * @brief SoftAP manager. See wifi_ap.h.
 *
 * Responsibilities: init netif/wifi, publish DHCP option 114 (captive portal URI), run the policy loop.
 * Dependencies: esp_wifi, esp_netif, esp_event, settings, ap_policy.
 * Threading: one manager task owns the policy and the radio state. Other tasks communicate with it through
 * volatile flags and a spinlock-protected status copy.
 */
#include "wifi_ap.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_policy.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "settings.h"
#include "wifi_scan_list.h"

static const char *TAG = "wifi_ap";

/** Policy evaluation period. One second also gives a 1 Hz AP countdown for the UI. */
#define MANAGER_PERIOD_MS 1000
/** Per-channel dwell of the active scan, ms (the driver default is 120). */
#define SCAN_DWELL_MIN_MS 100
#define SCAN_DWELL_MAX_MS 150
/** Most raw access-point records read from one scan (the clean list is shorter, see WIFI_SCAN_LIST_MAX). */
#define SCAN_RAW_MAX 40
/** Maximum simultaneous stations (the phone plus a spare). */
#define AP_MAX_STATIONS 4
/** A WebSocket heartbeat older than this means the web app is closed (client sends one every ~5 s). */
#define HEARTBEAT_TTL_MS 15000
/** Manager task stack size in bytes. */
#define MANAGER_STACK 4096
/** Manager task priority: below the Wi-Fi task and the HID path (future). */
#define MANAGER_PRIO 4
/** Interface key of the default SoftAP netif. */
#define AP_NETIF_KEY "WIFI_AP_DEF"
/** Captive-portal URI advertised through DHCP option 114 (the default SoftAP address). */
#define CAPTIVE_URI "http://192.168.4.1"

/** AP netif created by esp_netif_create_default_wifi_ap(). */
static esp_netif_t *s_netif;
/** true once wifi_ap_init() succeeded. */
static volatile bool s_initialized;
/** true while the Wi-Fi driver is started in AP mode. */
static bool s_ap_running;
/** Last heartbeat time (ms since boot), 0 = never. */
static volatile uint64_t s_last_heartbeat_ms;
/** OTA running flag. */
static volatile bool s_ota_active;
/** Host suspended flag from the power task. */
static volatile bool s_host_suspended;
/** Number of bonded BLE devices (set by the bridge); with none the AP stays on (first-run convenience). */
static volatile uint32_t s_bonded_devices;
/** One-shot "force on" request, consumed by the manager. */
static volatile bool s_force_on;
/** One-shot "off now" request, consumed by the manager. */
static volatile bool s_force_off;
/** One-shot "re-apply password" request, consumed by the manager. */
static volatile bool s_reconfigure;
/** Status copy for readers; guarded by s_lock. */
static wifi_ap_status_t s_status;
/** Guards s_status. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/** Station netif, created on first use by wifi_ap_sta_connect(). */
static esp_netif_t *s_sta_netif;
/** Event group signalling the station result. */
static EventGroupHandle_t s_sta_events;
/** Bit: the station got an IP address. */
#define STA_GOT_IP BIT0
/** Bit: the station gave up (wrong password, network not found). */
#define STA_FAILED BIT1
/** Connection attempts before giving up. */
#define STA_MAX_RETRIES 3
/** Attempts made so far for the current wifi_ap_sta_connect(). */
static int s_sta_retries;
/** true while a wifi_ap_sta_connect() is waiting (retries only then). */
static volatile bool s_sta_wanted;
/** true while wifi_ap_scan() owns the radio (a join and a second scan are refused meanwhile). */
static atomic_bool s_scanning;
/** Current SSID, built once from the MAC. */
static char s_ssid[33];

/** @brief Milliseconds since boot. */
static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Build the SSID "HubBLE-XXXX" from the last two bytes of the SoftAP MAC.
 */
static void build_ssid(void) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ssid, sizeof(s_ssid), "HubBLE-%02X%02X", mac[4], mac[5]);
}

/**
 * @brief Load the SoftAP configuration (SSID + stored password) into the driver.
 *
 * @return ESP_OK or the driver error.
 */
static esp_err_t apply_config(void) {
    wifi_config_t cfg = {0};
    cfg.ap.ssid_len = (uint8_t)strlen(s_ssid);
    memcpy(cfg.ap.ssid, s_ssid, cfg.ap.ssid_len);
    ESP_RETURN_ON_ERROR(settings_get_ap_password((char *)cfg.ap.password, sizeof(cfg.ap.password)), TAG, "password");
    cfg.ap.channel = CONFIG_HUBBLE_AP_CHANNEL;
    cfg.ap.max_connection = AP_MAX_STATIONS;
    /* WPA2-PSK only: WPA3/mixed modes break some phones' captive-portal detection and are not required. */
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    return esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

/** @brief Start the AP radio. */
static void ap_start(void) {
    if (apply_config() == ESP_OK && esp_wifi_start() == ESP_OK) {
        /* Units of 0.25 dBm. A lower power keeps the board cooler; the phone is a few metres away. */
        esp_wifi_set_max_tx_power((int8_t)(CONFIG_HUBBLE_WIFI_TX_POWER_DBM * 4));
        s_ap_running = true;
        ESP_LOGI(TAG, "AP on: SSID %s", s_ssid);
    } else {
        ESP_LOGE(TAG, "AP start failed");
    }
}

/** @brief Stop the AP radio. */
static void ap_stop(void) {
    esp_wifi_stop();
    s_ap_running = false;
    ESP_LOGI(TAG, "AP off");
}

/**
 * @brief Manager task: sample inputs, run the policy, start/stop the AP, publish the status.
 *
 * @param arg Unused.
 */
static void manager_task(void *arg) {
    (void)arg;
    ap_policy_t policy;
    const ap_policy_config_t cfg = {
        .boot_window_s = settings_get_boot_window_s(),
        .idle_timeout_s = settings_get_idle_timeout_s(),
    };
    ap_policy_init(&policy, &cfg);

    for (;;) {
        const uint64_t now = now_ms();
        wifi_sta_list_t sta = {0};
        if (s_ap_running) {
            esp_wifi_ap_get_sta_list(&sta);
        }
        const ap_policy_inputs_t in = {
            .now_ms = now,
#ifdef CONFIG_HUBBLE_ASSUME_BONDED
            .bonded_devices = 1, /* Test builds only. */
#else
            .bonded_devices = s_bonded_devices,
#endif
            .clients = (uint32_t)sta.num,
            .ws_alive = s_last_heartbeat_ms != 0 && (now - s_last_heartbeat_ms) < HEARTBEAT_TTL_MS,
            .ota_active = s_ota_active,
            .force_on = s_force_on,
            .force_off = s_force_off,
            .host_suspended = s_host_suspended,
        };
        s_force_on = false;
        s_force_off = false;
        const ap_policy_decision_t d = ap_policy_step(&policy, &in);

        if (s_reconfigure && s_ap_running) {
            s_reconfigure = false;
            ap_stop(); /* Restarting is the simplest way to apply a new password; stations must rejoin. */
        }
        if (d.ap_on && !s_ap_running) {
            ap_start();
        } else if (!d.ap_on && s_ap_running) {
            ap_stop();
        }

        portENTER_CRITICAL(&s_lock);
        s_status.on = s_ap_running;
        s_status.timeout_s = d.remaining_s;
        s_status.clients = (uint32_t)sta.num;
        strcpy(s_status.ssid, s_ssid);
        portEXIT_CRITICAL(&s_lock);

        vTaskDelay(pdMS_TO_TICKS(MANAGER_PERIOD_MS));
    }
}

/**
 * @brief Station events: IP obtained, or disconnected (retry a few times, then report failure).
 *
 * @param arg  Unused.
 * @param base Event base.
 * @param id   Event id.
 * @param data Event data.
 */
static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_sta_events, STA_GOT_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && s_sta_wanted) {
        if (++s_sta_retries < STA_MAX_RETRIES) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_sta_events, STA_FAILED);
        }
    }
}

/**
 * @brief Create the station netif and event handlers on first use (shared by the join and the scan).
 *
 * @return ESP_OK, or the first error.
 */
static esp_err_t sta_prepare(void) {
    if (s_sta_netif != NULL) {
        return ESP_OK;
    }
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    EventGroupHandle_t events = xEventGroupCreate();
    if (netif == NULL || events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_sta_events = events;
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event_handler, NULL), TAG,
                        "ip handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sta_event_handler, NULL),
                        TAG, "wifi handler");
    s_sta_netif = netif; /* Published last. */
    return ESP_OK;
}

/** @copydoc wifi_ap_sta_connect */
esp_err_t wifi_ap_sta_connect(const char *ssid, const char *password, uint32_t timeout_ms) {
    if (!s_initialized || !s_ap_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (atomic_load(&s_scanning)) {
        return ESP_ERR_INVALID_STATE; /* A scan is using the radio; the caller reports "busy". */
    }
    ESP_RETURN_ON_ERROR(sta_prepare(), TAG, "station");
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    cfg.sta.pmf_cfg.capable = true;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "apsta");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "sta config");
    xEventGroupClearBits(s_sta_events, STA_GOT_IP | STA_FAILED);
    s_sta_retries = 0;
    s_sta_wanted = true;
    esp_err_t err = esp_wifi_connect();
    EventBits_t bits = 0;
    if (err == ESP_OK) {
        bits = xEventGroupWaitBits(s_sta_events, STA_GOT_IP | STA_FAILED, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    }
    if (!(bits & STA_GOT_IP)) {
        wifi_ap_sta_disconnect();
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "station connected");
    return ESP_OK;
}

/** @copydoc wifi_ap_scan */
esp_err_t wifi_ap_scan(wifi_scan_net_t *out, size_t max, size_t *count) {
    *count = 0;
    if (!s_initialized || !s_ap_running || s_sta_wanted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (atomic_exchange(&s_scanning, true)) {
        return ESP_ERR_INVALID_STATE; /* Another scan is running. */
    }
    esp_err_t err = sta_prepare();
    wifi_mode_t prev = WIFI_MODE_AP;
    bool mode_changed = false;
    wifi_ap_record_t *recs = NULL;
    uint16_t n = 0;
    if (err == ESP_OK) {
        err = esp_wifi_get_mode(&prev);
    }
    if (err == ESP_OK && prev == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        mode_changed = err == ESP_OK;
    }
    if (err == ESP_OK) {
        /* Active scan, blocking. The call returns when every channel was visited (a couple of seconds at most). The
         * radio hops channels, but the AP keeps its own channel and the driver returns to it between hops (home-channel
         * dwell), so connected phones see short stalls, not a disconnect. */
        wifi_scan_config_t cfg = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
        cfg.scan_time.active.min = SCAN_DWELL_MIN_MS;
        cfg.scan_time.active.max = SCAN_DWELL_MAX_MS;
        err = esp_wifi_scan_start(&cfg, true);
    }
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_num(&n);
    }
    if (err == ESP_OK) {
        if (n > SCAN_RAW_MAX) {
            n = SCAN_RAW_MAX;
        }
        recs = calloc(SCAN_RAW_MAX, sizeof(*recs));
        err = recs != NULL ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&n, recs); /* Also releases the driver's copy of the results. */
    }
    if (err == ESP_OK) {
        wifi_scan_net_t *tmp = calloc(n ? n : 1, sizeof(*tmp));
        if (tmp == NULL) {
            err = ESP_ERR_NO_MEM;
        } else {
            for (uint16_t i = 0; i < n; i++) {
                strlcpy(tmp[i].ssid, (const char *)recs[i].ssid, sizeof(tmp[i].ssid));
                tmp[i].rssi = recs[i].rssi;
                tmp[i].secure = recs[i].authmode != WIFI_AUTH_OPEN;
            }
            const size_t kept = wifi_scan_list_clean(tmp, n, max);
            memcpy(out, tmp, kept * sizeof(*out));
            *count = kept;
            free(tmp);
        }
    }
    free(recs);
    if (mode_changed) {
        esp_wifi_set_mode(WIFI_MODE_AP); /* Back to AP-only, as wifi_ap_sta_disconnect() does. */
    }
    atomic_store(&s_scanning, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    }
    return err;
}

/** @copydoc wifi_ap_sta_disconnect */
void wifi_ap_sta_disconnect(void) {
    s_sta_wanted = false;
    if (s_sta_netif != NULL) {
        esp_wifi_disconnect();
        esp_wifi_set_mode(WIFI_MODE_AP);
    }
}

/** @copydoc wifi_ap_init */
esp_err_t wifi_ap_init(void) {
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_FAIL, TAG, "AP netif");

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wcfg), TAG, "wifi init");
    /* Keep Wi-Fi config in RAM only: our settings component owns persistence. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "mode");
    build_ssid();

    /* DHCP option 114 (RFC 8910) tells modern phones where the captive portal page is. The option can only be
     * changed while the DHCP server is stopped, which it is before the first esp_wifi_start(). */
    static char uri[] = CAPTIVE_URI;
    esp_err_t err = esp_netif_dhcps_option(s_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DHCP option 114 not set: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    BaseType_t ok = xTaskCreate(manager_task, "wifi_ap", MANAGER_STACK, NULL, MANAGER_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "manager task");
    return ESP_OK;
}

/** @copydoc wifi_ap_get_status */
void wifi_ap_get_status(wifi_ap_status_t *out) {
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

/** @copydoc wifi_ap_note_heartbeat */
void wifi_ap_note_heartbeat(void) {
    s_last_heartbeat_ms = now_ms();
}

/** @copydoc wifi_ap_set_ota_active */
void wifi_ap_set_ota_active(bool active) {
    s_ota_active = active;
}

/** @copydoc wifi_ap_set_bonded_devices */
void wifi_ap_set_bonded_devices(unsigned count) {
    s_bonded_devices = count;
}

/** @copydoc wifi_ap_set_host_suspended */
void wifi_ap_set_host_suspended(bool suspended) {
    s_host_suspended = suspended;
}

/** @copydoc wifi_ap_request_off */
void wifi_ap_request_off(void) {
    s_force_off = true;
}

/** @copydoc wifi_ap_request_on */
void wifi_ap_request_on(void) {
    s_force_on = true;
}

/** @copydoc wifi_ap_apply_password */
void wifi_ap_apply_password(void) {
    s_reconfigure = true;
}

/** @copydoc wifi_ap_is_initialized */
bool wifi_ap_is_initialized(void) {
    return s_initialized;
}
