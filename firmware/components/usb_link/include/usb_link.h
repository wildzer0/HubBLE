/**
 * @file usb_link.h
 * @brief USB configuration link: the web app over a vendor-defined HID interface (WebHID), no Wi-Fi needed.
 *
 * Responsibilities: receive the framed requests of the web app from the dongle's vendor HID interface (protocol in
 * ulink_proto.h), forward each one to the dongle's own HTTP API over the loopback interface (so every endpoint, its
 * validation and its CSRF guard exist exactly once), and send back the response and the events the WebSocket would
 * carry. The link answers requests only while the configuration window is open (boot window, gesture, no device paired
 * or an active session, see wifi_ap_config_open()): software on the computer cannot change the settings at will.
 * Dependencies: usb_hid, ulink_proto, wifi_ap, web_server, lwip sockets, cJSON, FreeRTOS.
 * Threading: one internal task owns the protocol; usb_hid calls the receive hook from the TinyUSB task (non-blocking)
 * and the event sink may be called from any task (non-blocking, drops when the queue is full).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the link (after usb_hid_init() and web_server_start()).
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM if the task or buffers cannot be created.
 */
esp_err_t usb_link_start(void);

#ifdef __cplusplus
}
#endif
