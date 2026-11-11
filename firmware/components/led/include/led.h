/**
 * @file led.h
 * @brief Status LED driver (WS2812 RGB or single GPIO LED, chosen by the board).
 *
 * Responsibilities: initialise the board's LED and set its colour.
 * On single-LED boards any non-zero colour switches the LED on.
 * Dependencies: board, led_strip (WS2812 boards).
 * Threading: not re-entrant; call from one task (the future LED pattern task).
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the LED hardware described by board_get_info().
 *
 * @return ESP_OK on success, otherwise the error from the underlying driver.
 * @note Must be called once before any other led_* function.
 */
esp_err_t led_init(void);

/**
 * @brief Set the LED colour.
 *
 * @param r Red intensity 0-255 (ignored on single-colour LEDs).
 * @param g Green intensity 0-255 (ignored on single-colour LEDs).
 * @param b Blue intensity 0-255 (ignored on single-colour LEDs).
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if led_init() was not called.
 */
esp_err_t led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Switch the LED off.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if led_init() was not called.
 */
esp_err_t led_off(void);

#ifdef __cplusplus
}
#endif
