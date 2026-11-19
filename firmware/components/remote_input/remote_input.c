/**
 * @file remote_input.c
 * @brief Phone as touchpad / keyboard. See remote_input.h.
 *
 * Dependencies: usb_hid, bridge, esp_timer, FreeRTOS, cJSON. Threading: see the header.
 */
#include "remote_input.h"

#include <string.h>

#include "ascii_hid.h"
#include "bridge.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid_report.h"
#include "usb_hid.h"

/** Largest pointer or scroll step accepted in one message (the USB report holds 16 bits; a phone swipe is far less). */
#define STEP_MAX 2000
/** Characters waiting to be typed; a message carries at most a few dozen, the page waits between chunks. */
#define TEXT_QUEUE_LEN 256
/** Time a typed key stays down, and the pause before the next one, ms: slow enough for any host to register it. */
#define KEY_DOWN_MS  12
#define KEY_PAUSE_MS 12
/** Period of the watchdog check while nothing is being typed, ms. */
#define WATCH_MS 250
/** A held button or key is released when the phone has been silent this long, ms (the page sends a repeat or a release
 *  well within it). */
#define STALE_MS 1500
/** Task stack and priority: it only calls usb_hid_*, but typing sleeps between keys. */
#define TASK_STACK 3072
#define TASK_PRIO  4
/** Mouse button bits accepted in `remote.buttons`: left, right, middle (the low bits of hid_mouse_button_t). */
#define BTN_MASK 0x07

/** End of the window in esp_timer microseconds; 0 = off. */
static volatile int64_t s_until_us;
/** Time of the last held-state message, microseconds. */
static volatile int64_t s_last_hold_us;
/** Buttons the phone holds now (a move must not release them: dragging). */
static volatile uint8_t s_buttons;
/** The phone holds a modifier or key now. */
static volatile bool s_keys_held;
/** Characters to type. */
static QueueHandle_t s_text_queue;

/**
 * @brief Is the feature usable right now?
 *
 * @return true while the window is open and the input is not locked.
 */
static bool active(void) {
    return s_until_us != 0 && esp_timer_get_time() < s_until_us && !bridge_is_locked();
}

/**
 * @brief Clamp an integer field of a message.
 *
 * @param msg Message.
 * @param key Field name.
 * @param lim Absolute limit.
 * @return The value within [-lim, lim]; 0 if the field is missing or not a number.
 */
static int32_t field(const cJSON *msg, const char *key, int32_t lim) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(msg, key);
    if (!cJSON_IsNumber(v)) {
        return 0;
    }
    double d = v->valuedouble;
    d = d > lim ? lim : (d < -lim ? -lim : d);
    return (int32_t)d;
}

/**
 * @brief Release everything this source holds on the host and forget what was queued to type.
 */
static void release_all(void) {
    s_buttons = 0;
    s_keys_held = false;
    if (s_text_queue != NULL) {
        xQueueReset(s_text_queue);
    }
    usb_hid_release_source(HID_SRC_REMOTE);
}

/** @copydoc remote_input_enable */
void remote_input_enable(void) {
    s_until_us = esp_timer_get_time() + (int64_t)REMOTE_INPUT_WINDOW_S * 1000000LL;
}

/** @copydoc remote_input_disable */
void remote_input_disable(void) {
    s_until_us = 0;
    release_all();
}

/** @copydoc remote_input_remaining_s */
uint32_t remote_input_remaining_s(void) {
    const int64_t left = s_until_us - esp_timer_get_time();
    return (s_until_us != 0 && left > 0) ? (uint32_t)((left + 999999) / 1000000) : 0;
}

/** @copydoc remote_input_handle */
void remote_input_handle(const char *event, const cJSON *msg) {
    if (!active()) {
        return;
    }
    if (strcmp(event, "remote.move") == 0) {
        usb_hid_mouse(HID_SRC_REMOTE, s_buttons, field(msg, "dx", STEP_MAX), field(msg, "dy", STEP_MAX), 0, 0);
    } else if (strcmp(event, "remote.scroll") == 0) {
        usb_hid_mouse(HID_SRC_REMOTE, s_buttons, 0, 0, field(msg, "dy", STEP_MAX), field(msg, "dx", STEP_MAX));
    } else if (strcmp(event, "remote.buttons") == 0) {
        const uint8_t b = (uint8_t)(field(msg, "b", 0xFF) & BTN_MASK);
        s_buttons = b;
        usb_hid_mouse(HID_SRC_REMOTE, b, 0, 0, 0, 0);
        s_last_hold_us = esp_timer_get_time();
    } else if (strcmp(event, "remote.key") == 0) {
        uint8_t keys[HID_KBD_MAX_KEYS];
        size_t n = 0;
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(msg, "k");
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (cJSON_IsNumber(it) && n < HID_KBD_MAX_KEYS && it->valueint > 0 && it->valueint < 0xE0) {
                keys[n++] = (uint8_t)it->valueint;
            }
        }
        const uint8_t mods = (uint8_t)field(msg, "m", 0xFF);
        usb_hid_keyboard(HID_SRC_REMOTE, mods, keys, n);
        s_keys_held = mods != 0 || n != 0;
        s_last_hold_us = esp_timer_get_time();
    } else if (strcmp(event, "remote.text") == 0 && s_text_queue != NULL) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(msg, "t");
        if (cJSON_IsString(t)) {
            for (const char *p = t->valuestring; *p != '\0'; p++) {
                xQueueSend(s_text_queue, p, 0); /* A full queue drops the rest: better than blocking the HTTP task. */
            }
        }
    }
}

/**
 * @brief Type one character (key down, pause, key up). Unmapped characters are skipped.
 *
 * @param c Character.
 */
static void type_char(char c) {
    ascii_hid_key_t k;
    if (!ascii_hid_lookup(c, &k)) {
        return;
    }
    usb_hid_keyboard(HID_SRC_REMOTE, k.modifiers, &k.usage, 1);
    vTaskDelay(pdMS_TO_TICKS(KEY_DOWN_MS));
    usb_hid_keyboard(HID_SRC_REMOTE, 0, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(KEY_PAUSE_MS));
}

/**
 * @brief Task: type queued characters and watch the window and the held state.
 *
 * @param arg Unused.
 */
static void remote_task(void *arg) {
    (void)arg;
    for (;;) {
        char c;
        if (xQueueReceive(s_text_queue, &c, pdMS_TO_TICKS(WATCH_MS)) == pdTRUE) {
            if (active()) {
                type_char(c);
            } else {
                xQueueReset(s_text_queue); /* Locked or expired while text was waiting: drop it. */
            }
            continue;
        }
        const int64_t now = esp_timer_get_time();
        const bool holding = s_buttons != 0 || s_keys_held;
        if (s_until_us != 0 && now >= s_until_us) {
            remote_input_disable(); /* The window ended. */
        } else if (holding && (bridge_is_locked() || now - s_last_hold_us > (int64_t)STALE_MS * 1000)) {
            release_all(); /* Locked, or the phone went quiet (page closed, Wi-Fi lost). */
        }
    }
}

/** @copydoc remote_input_init */
esp_err_t remote_input_init(void) {
    s_text_queue = xQueueCreate(TEXT_QUEUE_LEN, sizeof(char));
    if (s_text_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(remote_task, "remote_in", TASK_STACK, NULL, TASK_PRIO, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
