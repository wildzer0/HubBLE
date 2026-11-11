/**
 * @file board.c
 * @brief Board description built from the Kconfig-selected variant.
 *
 * Responsibilities: translate CONFIG_HUBBLE_* values into a board_info_t.
 * Dependencies: sdkconfig only. Threading: constant data, no state.
 */
#include "board.h"

#include "sdkconfig.h"

/**
 * Marker embedded in every image. The OTA upload scans the incoming stream for it (see ota/board_marker.h)
 * and refuses images built for another board (CLAUDE.md §3.8). Referenced from s_board so the linker keeps it.
 */
static const char s_image_marker[] = "HUBBLE_BOARD_ID=" CONFIG_HUBBLE_BOARD_ID ";";

/** The one and only board description; values come from components/board/Kconfig. */
static const board_info_t s_board = {
    .id = CONFIG_HUBBLE_BOARD_ID,
    .name = CONFIG_HUBBLE_BOARD_NAME,
#ifdef CONFIG_HUBBLE_LED_IS_WS2812
    .led_type = BOARD_LED_WS2812,
#else
    .led_type = BOARD_LED_GPIO,
#endif
    .led_gpio = CONFIG_HUBBLE_LED_GPIO,
#ifdef CONFIG_HUBBLE_LED_ACTIVE_LOW
    .led_active_low = true,
#else
    .led_active_low = false,
#endif
    .button_gpio = CONFIG_HUBBLE_BUTTON_GPIO,
    .image_marker = s_image_marker,
    .flash_size_mb = CONFIG_HUBBLE_FLASH_SIZE_MB,
};

/** @copydoc board_get_info */
const board_info_t *board_get_info(void) {
    return &s_board;
}
