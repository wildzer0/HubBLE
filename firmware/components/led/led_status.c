/**
 * @file led_status.c
 * @brief LED rendering task. See led_status.h.
 *
 * Dependencies: led, led_pattern, esp_timer, FreeRTOS. Threading: single task.
 */
#include "led_status.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"

/** Render period; fast enough for the 300 ms blinks and smooth breathing, cheap for a single pixel. */
#define LED_TICK_MS 50
/** Task stack and priority (low: purely cosmetic). */
#define LED_TASK_STACK 3072
#define LED_TASK_PRIO  2

/** Sampling callback. */
static void (*s_sample)(led_inputs_t *out);

/**
 * @brief Render loop: sample, compute, write the LED only when the colour changed.
 *
 * @param arg Unused.
 */
static void led_task(void *arg) {
    (void)arg;
    led_rgb_t last = {0, 0, 0};
    for (;;) {
        led_inputs_t in = {0};
        s_sample(&in);
        const led_rgb_t c = led_pattern(&in, (uint32_t)(esp_timer_get_time() / 1000));
        if (c.r != last.r || c.g != last.g || c.b != last.b) {
            if (c.r == 0 && c.g == 0 && c.b == 0) {
                led_off();
            } else {
                led_set_rgb(c.r, c.g, c.b);
            }
            last = c;
        }
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

/** @copydoc led_status_start */
esp_err_t led_status_start(void (*sample)(led_inputs_t *out)) {
    s_sample = sample;
    return xTaskCreate(led_task, "led", LED_TASK_STACK, NULL, LED_TASK_PRIO, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
