/**
 * @file led_status.h
 * @brief Task that renders the status LED patterns (CLAUDE.md §3.6) from led_pattern().
 *
 * Dependencies: led, led_pattern, esp_timer, FreeRTOS. Threading: one task owns the LED after led_status_start();
 * the sampling callback runs in that task and must not block.
 */
#pragma once

#include "esp_err.h"
#include "led_pattern.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the LED task.
 *
 * @param sample Fills the current inputs (called about 20 times per second).
 * @return ESP_OK, or ESP_ERR_NO_MEM if the task cannot be created.
 */
esp_err_t led_status_start(void (*sample)(led_inputs_t *out));

#ifdef __cplusplus
}
#endif
