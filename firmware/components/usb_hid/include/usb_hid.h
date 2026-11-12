/**
 * @file usb_hid.h
 * @brief USB composite HID device (boot keyboard + mouse/consumer/system) driven by TinyUSB.
 *
 * Responsibilities: install the TinyUSB device stack with the dongle's descriptors (CLAUDE.md §3.2), merge the
 * input of several sources (hid_report), send the reports to the host, track USB state (detached / configured /
 * suspended), receive the keyboard LED output report and trigger remote wakeup.
 * Dependencies: esp_tinyusb, hid_report, FreeRTOS.
 * Threading: usb_hid_init() once from app_main. The input functions (usb_hid_keyboard() ...) may be called
 * from any task and never block: they update the merge state under a short critical section and wake the
 * internal "usb_hid" task, which owns all TinyUSB report transmission. No dynamic allocation after init.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hid_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/** USB bus state as seen by the device. */
typedef enum {
    USB_STATE_DETACHED = 0, /**< Not enumerated: cable unplugged, host off, or powered only. */
    USB_STATE_CONFIGURED,   /**< The host enumerated and configured the device; reports flow. */
    USB_STATE_SUSPENDED,    /**< The host suspended the bus (sleep, or idle selective suspend). */
} usb_state_t;

/**
 * @brief Callback invoked (from the USB task) when the host changes the keyboard LED bits.
 *
 * @param leds hid_led_bits_t bit field (Num/Caps/Scroll Lock) the host wants on.
 */
typedef void (*usb_hid_leds_cb_t)(uint8_t leds);

/**
 * @brief Install the USB device and start the report task.
 *
 * @return ESP_OK, or the error from the TinyUSB driver / task creation. Calling twice returns ESP_ERR_INVALID_STATE.
 */
esp_err_t usb_hid_init(void);

/**
 * @brief Whether usb_hid_init() succeeded (used by the OTA self-test; says nothing about the host being connected).
 *
 * @return true if the driver is installed and the report task runs.
 */
bool usb_hid_is_initialized(void);

/**
 * @brief Current USB bus state.
 *
 * @return One of usb_state_t.
 */
usb_state_t usb_hid_get_state(void);

/**
 * @brief Human readable state name ("detached", "configured", "suspended") for the API.
 *
 * @param state State to describe.
 * @return Static string.
 */
const char *usb_hid_state_name(usb_state_t state);

/**
 * @brief Replace the keyboard state of a source (see hid_merge_set_keyboard()).
 *
 * While the bus is suspended and remote wakeup is allowed, a non-empty state wakes the host and is NOT
 * forwarded (CLAUDE.md §3.9).
 *
 * @param src       Source index (< HID_MERGE_SOURCES).
 * @param modifiers Modifier bit field.
 * @param keys      Pressed key usages.
 * @param nkeys     Number of entries in @p keys.
 */
void usb_hid_keyboard(unsigned src, uint8_t modifiers, const uint8_t *keys, size_t nkeys);

/**
 * @brief Replace the consumer-control usage of a source (0 = release).
 *
 * @param src   Source index.
 * @param usage Consumer usage (<= HID_CONSUMER_USAGE_MAX).
 */
void usb_hid_consumer(unsigned src, uint16_t usage);

/**
 * @brief Replace the system-control bits of a source (hid_system_bits_t).
 *
 * @param src  Source index.
 * @param bits Bit field.
 */
void usb_hid_system(unsigned src, uint8_t bits);

/**
 * @brief Update the mouse of a source: set its buttons and add relative movement.
 *
 * @param src     Source index.
 * @param buttons hid_mouse_button_t bit field.
 * @param dx      Relative X.
 * @param dy      Relative Y.
 * @param wheel   Vertical wheel.
 * @param pan     Horizontal wheel.
 */
void usb_hid_mouse(unsigned src, uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel, int32_t pan);

/**
 * @brief Release everything held by a source (call when its peripheral disconnects).
 *
 * @param src Source index.
 */
void usb_hid_release_source(unsigned src);

/**
 * @brief Keyboard LED bits last set by the host (hid_led_bits_t).
 *
 * @return Bit field.
 */
uint8_t usb_hid_get_leds(void);

/**
 * @brief Register the callback told about host keyboard LED changes (to forward them to BLE keyboards, M3).
 *
 * @param cb Callback, or NULL to remove.
 */
void usb_hid_set_leds_callback(usb_hid_leds_cb_t cb);

/**
 * @brief Allow or forbid waking a suspended host (setting "remote wakeup", default on).
 *
 * @param enabled true to allow.
 */
void usb_hid_set_remote_wakeup_allowed(bool enabled);

/**
 * @brief Prepare the USB peripheral for a reboot into the ROM download mode.
 *
 * Uninstalls the TinyUSB driver so the PHY is released before diag_reboot_to_download() resets the chip.
 * Safe to call when not initialised.
 */
void usb_hid_shutdown(void);

#ifdef __cplusplus
}
#endif
