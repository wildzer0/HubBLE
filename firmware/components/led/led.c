/**
 * @file led.c
 * @brief LED driver implementation (WS2812 via led_strip/RMT, or plain GPIO).
 *
 * Responsibilities: hide the board's LED type behind led_set_rgb().
 * Dependencies: board, led_strip, esp_driver_gpio.
 * Threading: static state is unprotected; use from a single task only.
 */
#include "led.h"

#include <stdbool.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "led_strip.h"

/** RMT tick rate for WS2812 timing; 10 MHz is the value used by the led_strip examples. */
#define LED_RMT_RESOLUTION_HZ (10 * 1000 * 1000)

/** Number of WS2812 pixels on the board (one on-board LED). */
#define LED_STRIP_PIXELS 1

static const char *TAG = "led";

/** Handle of the WS2812 strip; NULL on single-LED boards or before init. */
static led_strip_handle_t s_strip;
/** true once led_init() succeeded. */
static bool s_ready;

/**
 * @brief Drive a plain GPIO LED, honouring its polarity.
 *
 * @param on true to light the LED.
 * @return Result of gpio_set_level().
 */
static esp_err_t gpio_led_write(bool on) {
    const board_info_t *b = board_get_info();
    /* Active-low LEDs (e.g. XIAO user LED) light when the pin is pulled low. */
    const uint32_t level = (on != b->led_active_low) ? 1U : 0U;
    return gpio_set_level((gpio_num_t)b->led_gpio, level);
}

/** @copydoc led_init */
esp_err_t led_init(void) {
    const board_info_t *b = board_get_info();

    if (b->led_type == BOARD_LED_WS2812) {
        led_strip_config_t strip_cfg = {
            .strip_gpio_num = b->led_gpio,
            .max_leds = LED_STRIP_PIXELS,
        };
        led_strip_rmt_config_t rmt_cfg = {
            .resolution_hz = LED_RMT_RESOLUTION_HZ,
            .flags.with_dma = false, /* A single pixel does not need DMA. */
        };
        ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip), TAG, "RMT strip init failed");
    } else {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << b->led_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "GPIO config failed");
    }
    s_ready = true;
    return led_off();
}

/** @copydoc led_set_rgb */
esp_err_t led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (board_get_info()->led_type == BOARD_LED_WS2812) {
        ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_strip, 0, r, g, b), TAG, "set pixel failed");
        return led_strip_refresh(s_strip);
    }
    return gpio_led_write((r | g | b) != 0);
}

/** @copydoc led_off */
esp_err_t led_off(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (board_get_info()->led_type == BOARD_LED_WS2812) {
        return led_strip_clear(s_strip);
    }
    return gpio_led_write(false);
}
