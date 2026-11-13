/**
 * @file main.c
 * @brief HubBLE application entry point (M3: + BLE central and bridge).
 *
 * Responsibilities: bring the subsystems up in dependency order, wire the OTA progress to the WebSocket,
 * and register the OTA self-test checks.
 * Dependencies: board, led, usb_hid, bridge, ble_central, api, settings, log_buffer, ota, wifi_ap, captive_portal,
 * web_server, web_assets. Threading: runs once in the main task, then returns; every subsystem owns its own task(s).
 */
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "ble_central.h"
#include "board.h"
#include "bridge.h"
#include "captive_portal.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "led_status.h"
#include "log_hook.h"
#include "ota.h"
#include "ota_online.h"
#include "settings.h"
#include "usb_hid.h"
#include "usb_link.h"
#include "web_assets.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "hubble";

/** Size of the buffer holding a device event JSON. */
#define BLE_EVENT_JSON_MAX 640

/** Duration of the white boot flash (CLAUDE.md §3.6: "white, short"). */
#define BOOT_LED_FLASH_MS 300
/** Boot-flash brightness; kept low because WS2812 LEDs are very bright. */
/** How often the LED task re-reads brightness and the status-LED flag from NVS, ms. */
#define LED_SETTINGS_REFRESH_MS 1000
#define BOOT_LED_LEVEL          32
/** Red blink brightness used to signal "a rollback happened". */
#define ERROR_LED_LEVEL 64
/** Number of red blinks (CLAUDE.md §3.6: "red, 3 blinks"). */
#define ERROR_BLINKS 3
/** On/off time of one error blink. */
#define ERROR_BLINK_MS 250

/**
 * @brief Print the boot banner with version and board information.
 */
static void print_banner(void) {
    const esp_app_desc_t *app = esp_app_get_description();
    const board_info_t *board = board_get_info();

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, " HubBLE firmware v%s", app->version);
    ESP_LOGI(TAG, " Board   : %s (%s), %u MB flash", board->name, board->id, (unsigned)board->flash_size_mb);
    ESP_LOGI(TAG, " ESP-IDF : %s", app->idf_ver);
    ESP_LOGI(TAG, " Built   : %s %s", app->date, app->time);
    ESP_LOGI(TAG, "==============================");
}

/**
 * @brief Boot LED feedback: white flash, plus 3 red blinks if a rollback just happened.
 *
 * @param rollback true if the other OTA slot holds an invalidated image.
 */
static void boot_led(bool rollback) {
    if (led_init() != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed");
        return;
    }
    led_set_rgb(BOOT_LED_LEVEL, BOOT_LED_LEVEL, BOOT_LED_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(BOOT_LED_FLASH_MS));
    led_off();
    if (rollback) {
        for (int i = 0; i < ERROR_BLINKS; i++) {
            vTaskDelay(pdMS_TO_TICKS(ERROR_BLINK_MS));
            led_set_rgb(ERROR_LED_LEVEL, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(ERROR_BLINK_MS));
            led_off();
        }
    }
}

/**
 * @brief OTA progress -> WebSocket `ota.progress` event.
 *
 * @param received Bytes written.
 * @param total    Total bytes.
 */
static void on_ota_progress(uint32_t received, uint32_t total) {
    char json[64];
    snprintf(json, sizeof(json), "{\"received\":%u,\"total\":%u}", (unsigned)received, (unsigned)total);
    web_server_broadcast("ota.progress", json);
}

/** @brief Online update state or progress changed -> WebSocket `ota.state` event. */
static void on_ota_online_changed(void) {
    api_ota_events(web_server_broadcast);
}

/* Self-test checks (CLAUDE.md §3.8). The BLE check is added in M3. */
static bool check_settings(void) {
    return settings_is_ready();
}
static bool check_wifi(void) {
    return wifi_ap_is_initialized();
}
static bool check_web(void) {
    return web_server_is_running();
}
static bool check_ble(void) {
    return ble_central_is_ready();
}
static bool check_usb(void) {
    /* Driver installed and report task running; whether a host is attached is not a firmware fault. */
    return usb_hid_is_initialized();
}

/**
 * @brief Device/pairing change -> WebSocket event (`device.updated`, `device.removed`, `pairing.state`).
 *
 * @param slot Device slot, or -1 for a pairing state change.
 */
static void on_ble_changed(int slot) {
    char json[BLE_EVENT_JSON_MAX];
    if (slot < 0) {
        api_pairing_events(web_server_broadcast);
    } else if (api_device_event_json(slot, json, sizeof(json))) {
        web_server_broadcast(strstr(json, "\"removed\"") != NULL ? "device.removed" : "device.updated", json);
    }
}

/**
 * @brief Fill the status-LED inputs (called by the LED task about 20 times per second).
 *
 * Brightness and the "status LED" flag come from NVS and are re-read once per second at most.
 *
 * @param in Receives the current inputs.
 */
static void led_sample(led_inputs_t *in) {
    static uint32_t last_read_ms;
    static uint8_t brightness = SETTINGS_LED_BRIGHTNESS_DEFAULT;
    static bool status_led;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (last_read_ms == 0 || now - last_read_ms >= LED_SETTINGS_REFRESH_MS) {
        brightness = settings_get_led_brightness();
        status_led = settings_get_status_led();
        last_read_ms = now ? now : 1;
    }
    wifi_ap_status_t ap;
    wifi_ap_get_status(&ap);
    ble_pairing_status_t ps;
    ble_central_pairing_status(&ps);
    in->brightness = brightness;
    in->status_led = status_led;
    in->host_suspended = bridge_power_mode() == POWER_LOW;
    in->ota_active = ota_session_active();
    in->quarantine = ps.state == PAIR_QUARANTINE;
    in->pairing = ps.state == PAIR_SCANNING || ps.state == PAIR_CONNECTING || ps.state == PAIR_PAIRING ||
                  ps.state == PAIR_CONFIRM;
    in->ap_on = ap.on;
    in->connected = ble_central_connected_count();
}

/** @brief Entry point called by ESP-IDF after the system is up. */
void app_main(void) {
    /* The log hook comes first so that everything, including the banner, reaches the web app's log view. */
    log_hook_install();
    esp_err_t err = settings_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings init failed: %s", esp_err_to_name(err));
    }
    print_banner();
    ota_boot_check(); /* May roll back and reboot. */

    ota_status_t ota;
    ota_get_status(&ota);
    ESP_LOGI(TAG, "running slot %s, pending verify: %s, rollback happened: %s", ota.running_slot,
             ota.pending_verify ? "yes" : "no", ota.rollback_happened ? "yes" : "no");
    boot_led(ota.rollback_happened);

    size_t web_len = 0;
    const uint8_t *web = web_assets_get(&web_len);
    ESP_LOGI(TAG, "Embedded web bundle: %u bytes, %d files", (unsigned)web_len, web_bundle_count(web, web_len));

    err = usb_hid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB HID init failed: %s", esp_err_to_name(err));
    }
    usb_hid_set_remote_wakeup_allowed(settings_get_remote_wakeup()); /* CLAUDE.md §3.9, default on. */
    err = wifi_ap_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi AP init failed: %s", esp_err_to_name(err));
    }
    err = captive_portal_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "captive portal start failed: %s", esp_err_to_name(err));
    }
    err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web server start failed: %s", esp_err_to_name(err));
    }

    bridge_set_input_tap(web_server_input_active, web_server_broadcast_input); /* Input tester (M5). */
    err = bridge_init(on_ble_changed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE bridge init failed: %s", esp_err_to_name(err));
    }

    if (usb_hid_link_enabled()) {
        /* After BLE, so the radio gets its memory first. Open only while the configuration window is. */
        err = usb_link_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "USB link start failed: %s", esp_err_to_name(err));
        }
    }
    bridge_set_input_filter(settings_get_filter_enabled(), settings_get_filter_bounce_ms(),
                            settings_get_filter_hold_ms()); /* Typing filter, off unless the user turned it on. */
    led_status_start(led_sample); /* Status patterns (CLAUDE.md §3.6); off while the host sleeps. */
    ota_set_progress_cb(on_ota_progress);
    ota_online_set_changed_cb(on_ota_online_changed);
    ota_selftest_register("settings", check_settings);
    ota_selftest_register("wifi_ap", check_wifi);
    ota_selftest_register("web_server", check_web);
    ota_selftest_register("usb_hid", check_usb);
    ota_selftest_register("ble", check_ble);
    ota_selftest_start();
}
