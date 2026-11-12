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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "log_hook.h"
#include "ota.h"
#include "settings.h"
#include "usb_hid.h"
#include "web_assets.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "hubble";

/** Size of the buffer holding a device event JSON. */
#define BLE_EVENT_JSON_MAX 640

/** Duration of the white boot flash (CLAUDE.md §3.6: "white, short"). */
#define BOOT_LED_FLASH_MS 300
/** Boot-flash brightness; kept low because WS2812 LEDs are very bright. */
#define BOOT_LED_LEVEL 32
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
        web_server_broadcast("pairing.state", "{}");
    } else if (api_device_event_json(slot, json, sizeof(json))) {
        web_server_broadcast(strstr(json, "\"removed\"") != NULL ? "device.removed" : "device.updated", json);
    }
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

    err = bridge_init(on_ble_changed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE bridge init failed: %s", esp_err_to_name(err));
    }

    ota_set_progress_cb(on_ota_progress);
    ota_selftest_register("settings", check_settings);
    ota_selftest_register("wifi_ap", check_wifi);
    ota_selftest_register("web_server", check_web);
    ota_selftest_register("usb_hid", check_usb);
    ota_selftest_register("ble", check_ble);
    ota_selftest_start();
}
