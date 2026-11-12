/**
 * @file bridge.c
 * @brief BLE -> USB forwarding glue (see bridge.h).
 *
 * Dependencies: ble_central, usb_hid, hid_normalizer, combo, wifi_ap, settings, esp_timer, FreeRTOS.
 * Threading: see bridge.h.
 */
#include "bridge.h"

#include "ble_central.h"
#include "combo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid_normalizer.h"
#include "settings.h"
#include "usb_hid.h"
#include "wifi_ap.h"

static const char *TAG = "bridge";

/** Period of the combo poll in ms: finer than needed for 3 s holds, coarse enough to be free. */
#define COMBO_POLL_MS 100
/** Combo task stack size. */
#define COMBO_TASK_STACK 3072
/** Combo task priority (low: it is not on the forwarding path). */
#define COMBO_TASK_PRIO 3

/** Depth of the input-tester queue (events beyond it are dropped: the tester is best effort). */
#define TAP_QUEUE_LEN 16
/** Tap task stack and priority (low: never competes with forwarding). */
#define TAP_TASK_STACK 3072
#define TAP_TASK_PRIO  2
/** Pause after each emitted event, ms: caps the stream at ~50 events/s so a 125 Hz mouse cannot flood the Wi-Fi. */
#define TAP_PACE_MS 20
/** Largest `input.event` JSON object. */
#define TAP_JSON_MAX 160

/** One queued input-tester event. */
typedef struct {
    uint8_t slot;        /**< Source device slot. */
    hid_norm_event_t ev; /**< The decoded event. */
} tap_item_t;

static void (*s_on_changed)(int slot);
static bool (*s_tap_active)(void);
static void (*s_tap_emit)(const char *json);
static QueueHandle_t s_tap_queue;

/**
 * @brief Forward one decoded report to USB as source @p slot.
 *
 * Never logs the report content (CLAUDE.md §3.12).
 *
 * @param slot      Device slot.
 * @param layout    Device layout.
 * @param report_id Report ID.
 * @param data      Payload.
 * @param len       Payload length.
 * @param rx_us     Reception timestamp.
 */
static void on_report(unsigned slot, const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len,
                      int64_t rx_us) {
    hid_norm_event_t ev;
    if (!hid_norm_decode(layout, report_id, data, len, &ev)) {
        return;
    }
    if (ble_central_quarantined(slot)) {
        /* Quarantine (CLAUDE.md §3.4): a Just Works device being paired is connected but MUTED. Its input only
         * feeds the pairing challenge and never reaches the host. */
        if (ev.has_mouse) {
            ble_central_challenge_buttons(ev.buttons);
        }
        if (ev.has_keyboard) {
            ble_central_challenge_keys(ev.keys);
        }
        return;
    }
    if (s_tap_active != NULL && s_tap_queue != NULL && s_tap_active()) {
        const tap_item_t item = {.slot = (uint8_t)slot, .ev = ev};
        xQueueSend(s_tap_queue, &item, 0); /* Never blocks: drop when full. */
    }
    if (ev.has_keyboard) {
        usb_hid_keyboard(slot, ev.modifiers, ev.keys, HID_KBD_MAX_KEYS);
    }
    if (ev.has_consumer) {
        usb_hid_consumer(slot, ev.consumer);
    }
    if (ev.has_system) {
        usb_hid_system(slot, ev.system);
    }
    if (ev.has_mouse) {
        usb_hid_mouse(slot, ev.buttons, ev.dx, ev.dy, ev.wheel, ev.pan);
    }
    int64_t us = esp_timer_get_time() - rx_us;
    ble_central_record_latency(slot, us < 0 ? 0 : (uint32_t)us);
}

/**
 * @brief A device connected or disconnected. On disconnect everything it held is released, so no key can stay
 *        stuck on the host.
 *
 * @param slot      Device slot.
 * @param connected true if now connected.
 */
static void on_link(unsigned slot, bool connected) {
    if (!connected) {
        usb_hid_release_source(slot);
    }
}

/**
 * @brief Host keyboard LED change -> every BLE keyboard (runs in the TinyUSB task).
 *
 * @param leds hid_led_bits_t bit field.
 */
static void on_host_leds(uint8_t leds) {
    ble_central_write_leds(leds);
}

/**
 * @brief Forward a changed notification to the registered listener.
 *
 * @param slot Slot or -1.
 */
static void on_changed(int slot) {
    if (s_on_changed != NULL) {
        s_on_changed(slot);
    }
}

/**
 * @brief Combo task: feed the merged keyboard state to the detector and act on its events.
 *
 * @param arg Unused.
 */
static void combo_task(void *arg) {
    (void)arg;
    combo_t combo;
    combo_init(&combo, 0); /* Hold time becomes a setting in M5. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(COMBO_POLL_MS));
        hid_kbd_report_t k;
        usb_hid_get_keyboard_state(&k);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        switch (combo_update(&combo, now_ms, k.modifiers, k.keys, HID_KBD_MAX_KEYS)) {
        case COMBO_AP_ON:
            ESP_LOGI(TAG, "both Shifts held: Wi-Fi AP on");
            wifi_ap_request_on();
            /* The same gesture is the physical confirmation of a pairing waiting for it (CLAUDE.md §3.4.3). The
             * candidate is quarantined, so this can only come from an already paired keyboard. */
            ble_central_physical_confirm();
            break;
        case COMBO_RECOVERY:
            ESP_LOGW(TAG, "recovery combo: resetting the Wi-Fi password");
            if (settings_reset_ap_password() == ESP_OK) {
                wifi_ap_apply_password();
            }
            wifi_ap_request_on();
            break;
        case COMBO_NONE:
        default:
            break;
        }
    }
}

/**
 * @brief Format and emit one queued event as the `data` JSON of `input.event`.
 *
 * Key usages are sent as numbers; the web app turns them into names. A report can carry several kinds at once, so up
 * to three messages are produced.
 *
 * @param it The event.
 */
static void tap_emit_item(const tap_item_t *it) {
    char buf[TAP_JSON_MAX];
    const hid_norm_event_t *e = &it->ev;
    if (e->has_keyboard) {
        int n = snprintf(buf, sizeof(buf), "{\"slot\":%u,\"kind\":\"key\",\"modifiers\":%u,\"keys\":[", it->slot,
                         e->modifiers);
        bool first = true;
        for (int i = 0; i < HID_KBD_MAX_KEYS && n > 0 && n < (int)sizeof(buf) - 8; i++) {
            if (e->keys[i] != 0) {
                n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s%u", first ? "" : ",", e->keys[i]);
                first = false;
            }
        }
        snprintf(buf + n, sizeof(buf) - (size_t)n, "]}");
        s_tap_emit(buf);
    }
    if (e->has_consumer) {
        snprintf(buf, sizeof(buf), "{\"slot\":%u,\"kind\":\"consumer\",\"usage\":%u}", it->slot, e->consumer);
        s_tap_emit(buf);
    }
    if (e->has_mouse) {
        snprintf(buf, sizeof(buf),
                 "{\"slot\":%u,\"kind\":\"mouse\",\"buttons\":%u,\"dx\":%d,\"dy\":%d,\"wheel\":%d,\"pan\":%d}",
                 it->slot, e->buttons, (int)e->dx, (int)e->dy, (int)e->wheel, (int)e->pan);
        s_tap_emit(buf);
    }
}

/**
 * @brief Input-tester task: drains the queue and emits events, paced.
 *
 * @param arg Unused.
 */
static void tap_task(void *arg) {
    (void)arg;
    tap_item_t it;
    for (;;) {
        if (xQueueReceive(s_tap_queue, &it, portMAX_DELAY) == pdTRUE) {
            tap_emit_item(&it);
            vTaskDelay(pdMS_TO_TICKS(TAP_PACE_MS));
        }
    }
}

/** @copydoc bridge_set_input_tap */
void bridge_set_input_tap(bool (*active)(void), void (*emit)(const char *json)) {
    s_tap_active = active;
    s_tap_emit = emit;
}

/** @copydoc bridge_init */
esp_err_t bridge_init(void (*on_changed_cb)(int slot)) {
    s_on_changed = on_changed_cb;
    const ble_central_cbs_t cbs = {.on_report = on_report, .on_link = on_link, .on_changed = on_changed};
    usb_hid_set_leds_callback(on_host_leds);
    if (s_tap_emit != NULL) {
        s_tap_queue = xQueueCreate(TAP_QUEUE_LEN, sizeof(tap_item_t));
        if (s_tap_queue == NULL ||
            xTaskCreate(tap_task, "input_tap", TAP_TASK_STACK, NULL, TAP_TASK_PRIO, NULL) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xTaskCreate(combo_task, "combo", COMBO_TASK_STACK, NULL, COMBO_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ble_central_init(&cbs);
}
