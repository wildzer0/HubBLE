/**
 * @file main.c
 * @brief HubBLE application entry point (M0: boot banner and LED check only).
 *
 * Responsibilities: print the boot banner (version, board, IDF version), flash
 * the status LED white briefly, prove the embedded web bundle is linked, and
 * confirm the running image for OTA rollback.
 * Dependencies: board, led, web_assets, esp_app_desc, app_update.
 * Threading: runs once in the main task; later milestones spawn dedicated tasks.
 */
#include <stdio.h>

#include "board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "web_assets.h"

static const char *TAG = "hubble";

/** Duration of the white boot flash (CLAUDE.md §3.6: "white, short"). */
#define BOOT_LED_FLASH_MS 300
/** Boot-flash brightness; kept low because WS2812 LEDs are very bright. */
#define BOOT_LED_LEVEL 32

/**
 * @brief Print the boot banner with version and board information.
 *
 * Side effects: writes to the ESP log only.
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

/** @brief Entry point called by ESP-IDF after the system is up. */
void app_main(void) {
    print_banner();

    esp_err_t err = led_init();
    if (err == ESP_OK) {
        led_set_rgb(BOOT_LED_LEVEL, BOOT_LED_LEVEL, BOOT_LED_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(BOOT_LED_FLASH_MS));
        led_off();
    } else {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(err));
    }

    /* Prove the embedded web app pipeline: the HTTP server arrives in M1. */
    size_t web_len = 0;
    const uint8_t *web = web_assets_get(&web_len);
    ESP_LOGI(TAG, "Embedded web bundle: %u bytes, %d files", (unsigned)web_len, web_bundle_count(web, web_len));

    /* Rollback is enabled in sdkconfig. Until the real self-test exists (M1), confirm the image
     * here so a fresh flash is not rolled back on the next reset. */
    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Could not mark app valid: %s", esp_err_to_name(err));
    }
}
