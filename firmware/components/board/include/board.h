/**
 * @file board.h
 * @brief Board abstraction: the single place that knows per-board hardware facts.
 *
 * Responsibilities: expose LED type/GPIO, button GPIO and flash size of the
 * selected board (chosen at build time via Kconfig, see CLAUDE.md §2.3).
 * No other component may hard-code GPIO numbers.
 * Dependencies: none (pure constants from sdkconfig).
 * Threading: the returned data is immutable; safe from any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Kind of status LED fitted to the board. */
typedef enum {
    BOARD_LED_WS2812 = 0, /**< Addressable RGB LED driven through the RMT peripheral. */
    BOARD_LED_GPIO,       /**< Single plain LED driven directly by a GPIO. */
} board_led_type_t;

/** Immutable description of the selected board. */
typedef struct {
    const char *id;            /**< Short machine identifier, e.g. "s3_zero" (used in OTA manifests). */
    const char *name;          /**< Human readable name. */
    board_led_type_t led_type; /**< Which LED driver to use. */
    int led_gpio;              /**< GPIO the LED is wired to. */
    bool led_active_low;       /**< true if the plain LED lights when the GPIO is low (GPIO LEDs only). */
    int button_gpio;           /**< GPIO of the BOOT button (last-resort recovery only). */
    const char *image_marker;  /**< "HUBBLE_BOARD_ID=<id>;" string embedded in the image so OTA can check the board. */
    uint32_t flash_size_mb;    /**< Total flash size in MiB. */
} board_info_t;

/**
 * @brief Get the description of the board this firmware was built for.
 *
 * @return Pointer to a static, immutable structure (never NULL).
 * @note Thread-safe; no side effects.
 */
const board_info_t *board_get_info(void);

#ifdef __cplusplus
}
#endif
