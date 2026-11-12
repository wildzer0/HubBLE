/**
 * @file usb_hid.c
 * @brief TinyUSB composite HID device: descriptors, report transmission, suspend/resume, LED output.
 *
 * Responsibilities: see usb_hid.h.
 * Dependencies: esp_tinyusb (TinyUSB device stack), hid_report (descriptors + merge), FreeRTOS.
 * Threading model:
 *  - producers (BLE tasks, HTTP task) call usb_hid_*() -> they take s_lock (a short spinlock section, no
 *    blocking calls inside), update s_merge and the dirty flags, then notify the report task;
 *  - the "usb_hid" task is the only caller of tud_hid_n_report();
 *  - TinyUSB callbacks (tinyusb_event_cb, tud_hid_*_cb) run in the TinyUSB task and only touch atomics or
 *    take s_lock briefly.
 */
#include "usb_hid.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "usb_hid";

/** HID interface indices (also the TinyUSB HID instance numbers). */
enum { ITF_KEYBOARD = 0, ITF_CONTROL = 1, ITF_COUNT = 2 };
/** IN endpoint addresses. */
#define EP_KEYBOARD_IN 0x81
#define EP_CONTROL_IN  0x82
/** Endpoint packet size: the largest report is 1 (ID) + 7 (mouse) bytes. */
#define EP_SIZE 8
/** Endpoint polling interval in ms (full speed, CLAUDE.md §3.2). */
#define POLL_INTERVAL_MS 1
/** Bus power requested from the host, mA (the dongle is a bus-powered device). */
#define USB_MAX_POWER_MA 200
/** String descriptor indices (index 0 is the language ID, handled by esp_tinyusb). */
enum { STR_LANG = 0, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_COUNT };
/** Stack size of the report task. */
#define REPORT_TASK_STACK 3072
/** Priority of the report task: above the HTTP/Wi-Fi tasks so HID never waits for them. */
#define REPORT_TASK_PRIO 6
/** Retry period while the endpoint is busy or movement is still queued. */
#define RETRY_TICKS pdMS_TO_TICKS(1)
/** Minimum spacing between remote-wakeup attempts, to avoid hammering a host that ignores them. */
#define WAKEUP_MIN_INTERVAL_MS 500

/** Total length of the configuration descriptor. */
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)

/** Device descriptor: composite via interfaces (class 0), full speed, VID/PID from Kconfig. */
static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = CONFIG_HUBBLE_USB_VID,
    .idProduct = CONFIG_HUBBLE_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

/**
 * String descriptors. Index 0 holds the language ID (0x0409, little endian) in its first two bytes; the
 * serial is left NULL so esp_tinyusb fills it with the eFuse MAC.
 */
static const char *s_strings[STR_COUNT] = {
    [STR_LANG] = "\x09\x04",
    [STR_MANUFACTURER] = "HubBLE Project",
    [STR_PRODUCT] = "HubBLE",
    [STR_SERIAL] = NULL,
};

/**
 * Configuration descriptor. Remote wakeup is advertised (CLAUDE.md §3.2). Interface 0 is a boot-protocol
 * keyboard (subclass 1 / protocol 1) so BIOS/UEFI accept it; interface 1 is a plain HID interface.
 */
static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, USB_MAX_POWER_MA),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, HID_KBD_REPORT_DESC_LEN, EP_KEYBOARD_IN, EP_SIZE,
                       POLL_INTERVAL_MS),
    TUD_HID_DESCRIPTOR(ITF_CONTROL, 0, HID_ITF_PROTOCOL_NONE, HID_CTRL_REPORT_DESC_LEN, EP_CONTROL_IN, EP_SIZE,
                       POLL_INTERVAL_MS),
};

/** Which reports have changed since they were last sent (bit mask). */
enum {
    DIRTY_KEYBOARD = 1u << 0,
    DIRTY_MOUSE = 1u << 1,
    DIRTY_CONSUMER = 1u << 2,
    DIRTY_SYSTEM = 1u << 3,
    DIRTY_ALL = DIRTY_KEYBOARD | DIRTY_MOUSE | DIRTY_CONSUMER | DIRTY_SYSTEM,
};

/** Protects s_merge, s_dirty and the last-sent copies; held only for a few memory operations. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/** Merged input of all sources. Guarded by s_lock. */
static hid_merge_t s_merge;
/** DIRTY_* mask of reports still to be sent. Guarded by s_lock. */
static uint8_t s_dirty;
/** Current bus state (usb_state_t); written by the TinyUSB task, read anywhere. */
static volatile usb_state_t s_state = USB_STATE_DETACHED;
/** Host enabled remote wakeup for the current suspend (from the suspend event). */
static volatile bool s_host_allows_wakeup;
/** Application setting: may we wake the host? */
static volatile bool s_wakeup_allowed = true;
/** Keyboard LED bits from the host. */
static volatile uint8_t s_leds;
/** LED callback. */
static volatile usb_hid_leds_cb_t s_leds_cb;
/** Report task handle (notified by producers); NULL until init. */
static TaskHandle_t s_task;
/** True once the driver and task are up. */
static volatile bool s_initialized;
/** Tick count of the last wakeup attempt. */
static TickType_t s_last_wakeup_tick;

/**
 * @brief Wake the report task (safe from any task, never blocks).
 */
static void kick(void) {
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

/**
 * @brief Mark reports dirty and wake the task.
 *
 * @param mask DIRTY_* bits.
 */
static void mark_dirty(uint8_t mask) {
    portENTER_CRITICAL(&s_lock);
    s_dirty |= mask;
    portEXIT_CRITICAL(&s_lock);
    kick();
}

/**
 * @brief If the bus is suspended, try to wake the host (rate limited).
 *
 * Called by the input functions for events that carry activity. Must be called WITHOUT s_lock held.
 *
 * @return true if the bus is suspended, i.e. the caller must drop the event: the waking input is not
 *         forwarded (CLAUDE.md §3.9) and the host will resync on resume.
 */
static bool wake_if_suspended(void) {
    if (s_state != USB_STATE_SUSPENDED) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if (s_wakeup_allowed && s_host_allows_wakeup &&
        (now - s_last_wakeup_tick) >= pdMS_TO_TICKS(WAKEUP_MIN_INTERVAL_MS)) {
        s_last_wakeup_tick = now;
        esp_err_t err = tinyusb_remote_wakeup();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "remote wakeup failed: %s", esp_err_to_name(err));
        }
    }
    return true;
}

/**
 * @brief TinyUSB bus event callback (runs in the TinyUSB task).
 *
 * @param event Event description.
 * @param arg   Unused.
 */
static void on_usb_event(tinyusb_event_t *event, void *arg) {
    (void)arg;
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_state = USB_STATE_CONFIGURED;
        /* A fresh host knows nothing about our state: send everything once. */
        mark_dirty(DIRTY_ALL);
        break;
    case TINYUSB_EVENT_DETACHED:
        s_state = USB_STATE_DETACHED;
        break;
    case TINYUSB_EVENT_SUSPENDED:
        s_host_allows_wakeup = event->suspended.remote_wakeup;
        s_state = USB_STATE_SUSPENDED;
        break;
    case TINYUSB_EVENT_RESUMED:
        s_state = USB_STATE_CONFIGURED;
        /* Restore the host's view of held keys immediately. */
        mark_dirty(DIRTY_ALL);
        break;
    }
    ESP_LOGI(TAG, "USB state -> %s", usb_hid_state_name(s_state));
}

/**
 * @brief Send at most one pending report (called only by the report task).
 *
 * Order: keyboard (own interface, always independent), then one of mouse / consumer / system, because the
 * control interface has a single IN endpoint that must be idle before the next report.
 */
static void send_pending(void) {
    uint8_t dirty;
    hid_kbd_report_t kbd;
    hid_mouse_in_t mouse;
    uint16_t consumer;
    uint8_t system;
    bool mouse_left;

    portENTER_CRITICAL(&s_lock);
    dirty = s_dirty;
    portEXIT_CRITICAL(&s_lock);
    if (dirty == 0) {
        return;
    }

    if ((dirty & DIRTY_KEYBOARD) && tud_hid_n_ready(ITF_KEYBOARD)) {
        portENTER_CRITICAL(&s_lock);
        hid_merge_get_keyboard(&s_merge, &kbd);
        s_dirty &= (uint8_t)~DIRTY_KEYBOARD;
        portEXIT_CRITICAL(&s_lock);
        /* Report ID 0: the boot keyboard interface has no report IDs. */
        if (!tud_hid_n_report(ITF_KEYBOARD, 0, &kbd, sizeof(kbd))) {
            mark_dirty(DIRTY_KEYBOARD);
        }
    }

    if (!tud_hid_n_ready(ITF_CONTROL)) {
        return;
    }
    if (dirty & DIRTY_MOUSE) {
        portENTER_CRITICAL(&s_lock);
        hid_merge_take_mouse(&s_merge, &mouse);
        mouse_left = hid_merge_mouse_pending(&s_merge); /* clamped remainder: more to send */
        if (!mouse_left) {
            s_dirty &= (uint8_t)~DIRTY_MOUSE;
        }
        portEXIT_CRITICAL(&s_lock);
        if (!tud_hid_n_report(ITF_CONTROL, HID_RID_MOUSE, &mouse, sizeof(mouse))) {
            mark_dirty(DIRTY_MOUSE); /* NB: the drained movement of this report is lost; endpoint was ready. */
        }
    } else if (dirty & DIRTY_CONSUMER) {
        portENTER_CRITICAL(&s_lock);
        consumer = hid_merge_get_consumer(&s_merge);
        s_dirty &= (uint8_t)~DIRTY_CONSUMER;
        portEXIT_CRITICAL(&s_lock);
        if (!tud_hid_n_report(ITF_CONTROL, HID_RID_CONSUMER, &consumer, sizeof(consumer))) {
            mark_dirty(DIRTY_CONSUMER);
        }
    } else if (dirty & DIRTY_SYSTEM) {
        portENTER_CRITICAL(&s_lock);
        system = hid_merge_get_system(&s_merge);
        s_dirty &= (uint8_t)~DIRTY_SYSTEM;
        portEXIT_CRITICAL(&s_lock);
        if (!tud_hid_n_report(ITF_CONTROL, HID_RID_SYSTEM, &system, sizeof(system))) {
            mark_dirty(DIRTY_SYSTEM);
        }
    }
}

/**
 * @brief Report task: waits for producers, then drains the dirty reports at the endpoint's pace.
 *
 * @param arg Unused.
 */
static void report_task(void *arg) {
    (void)arg;
    for (;;) {
        bool pending;
        portENTER_CRITICAL(&s_lock);
        pending = s_dirty != 0;
        portEXIT_CRITICAL(&s_lock);
        /* Sleep until kicked; while something is pending, retry every tick (endpoint busy). */
        ulTaskNotifyTake(pdTRUE, pending ? RETRY_TICKS : portMAX_DELAY);
        if (s_state == USB_STATE_CONFIGURED) {
            send_pending();
        } else {
            /* Not connected / suspended: the host will get a full resync on (re)attach, so drop the queue
             * and the unsent movement instead of replaying stale input later. */
            portENTER_CRITICAL(&s_lock);
            s_dirty = 0;
            hid_mouse_in_t discard;
            hid_merge_take_mouse(&s_merge, &discard);
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

/** @copydoc usb_hid_init */
esp_err_t usb_hid_init(void) {
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    hid_merge_init(&s_merge);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(on_usb_event);
    cfg.descriptor.device = &s_device_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = STR_COUNT;
    cfg.descriptor.full_speed_config = s_config_desc;
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(report_task, "usb_hid", REPORT_TASK_STACK, NULL, REPORT_TASK_PRIO, &s_task) != pdPASS) {
        tinyusb_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "USB HID ready (VID %04X PID %04X)", CONFIG_HUBBLE_USB_VID, CONFIG_HUBBLE_USB_PID);
    return ESP_OK;
}

/** @copydoc usb_hid_is_initialized */
bool usb_hid_is_initialized(void) {
    return s_initialized;
}

/** @copydoc usb_hid_get_state */
usb_state_t usb_hid_get_state(void) {
    return s_state;
}

/** @copydoc usb_hid_state_name */
const char *usb_hid_state_name(usb_state_t state) {
    switch (state) {
    case USB_STATE_CONFIGURED:
        return "configured";
    case USB_STATE_SUSPENDED:
        return "suspended";
    case USB_STATE_DETACHED:
    default:
        return "detached";
    }
}

/** @copydoc usb_hid_keyboard */
void usb_hid_keyboard(unsigned src, uint8_t modifiers, const uint8_t *keys, size_t nkeys) {
    bool activity = modifiers != 0 || nkeys != 0;
    if (activity && wake_if_suspended()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    hid_merge_set_keyboard(&s_merge, src, modifiers, keys, nkeys);
    portEXIT_CRITICAL(&s_lock);
    mark_dirty(DIRTY_KEYBOARD);
}

/** @copydoc usb_hid_consumer */
void usb_hid_consumer(unsigned src, uint16_t usage) {
    if (usage != 0 && wake_if_suspended()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    hid_merge_set_consumer(&s_merge, src, usage);
    portEXIT_CRITICAL(&s_lock);
    mark_dirty(DIRTY_CONSUMER);
}

/** @copydoc usb_hid_system */
void usb_hid_system(unsigned src, uint8_t bits) {
    if (bits != 0 && wake_if_suspended()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    hid_merge_set_system(&s_merge, src, bits);
    portEXIT_CRITICAL(&s_lock);
    mark_dirty(DIRTY_SYSTEM);
}

/** @copydoc usb_hid_mouse */
void usb_hid_mouse(unsigned src, uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel, int32_t pan) {
    bool activity = buttons != 0 || dx != 0 || dy != 0 || wheel != 0 || pan != 0;
    if (activity && wake_if_suspended()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    hid_merge_mouse(&s_merge, src, buttons, dx, dy, wheel, pan);
    portEXIT_CRITICAL(&s_lock);
    mark_dirty(DIRTY_MOUSE);
}

/** @copydoc usb_hid_release_source */
void usb_hid_release_source(unsigned src) {
    portENTER_CRITICAL(&s_lock);
    hid_merge_clear_source(&s_merge, src);
    portEXIT_CRITICAL(&s_lock);
    mark_dirty(DIRTY_ALL);
}

/** @copydoc usb_hid_get_keyboard_state */
void usb_hid_get_keyboard_state(hid_kbd_report_t *out) {
    portENTER_CRITICAL(&s_lock);
    hid_merge_get_keyboard(&s_merge, out);
    portEXIT_CRITICAL(&s_lock);
}

/** @copydoc usb_hid_get_leds */
uint8_t usb_hid_get_leds(void) {
    return s_leds;
}

/** @copydoc usb_hid_set_leds_callback */
void usb_hid_set_leds_callback(usb_hid_leds_cb_t cb) {
    s_leds_cb = cb;
}

/** @copydoc usb_hid_set_remote_wakeup_allowed */
void usb_hid_set_remote_wakeup_allowed(bool enabled) {
    s_wakeup_allowed = enabled;
}

/** @copydoc usb_hid_shutdown */
void usb_hid_shutdown(void) {
    if (s_initialized) {
        s_initialized = false;
        tinyusb_driver_uninstall();
    }
}

/* ---- TinyUSB HID class callbacks (TinyUSB task) ---------------------------------------------------- */

/**
 * @brief GET_REPORT descriptor request: return the report descriptor of an interface.
 *
 * @param instance HID interface index.
 * @return Descriptor bytes.
 */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return instance == ITF_KEYBOARD ? hid_kbd_report_desc : hid_ctrl_report_desc;
}

/**
 * @brief GET_REPORT control request: the host polls the current input state (boot keyboards do this).
 *
 * @param instance    HID interface index.
 * @param report_id   Requested report ID (0 on the keyboard interface).
 * @param report_type Input / output / feature.
 * @param buffer      Destination.
 * @param reqlen      Space available.
 * @return Bytes written; 0 makes the stack STALL the request.
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)report_id;
    if (instance != ITF_KEYBOARD || report_type != HID_REPORT_TYPE_INPUT || reqlen < sizeof(hid_kbd_report_t)) {
        return 0;
    }
    hid_kbd_report_t kbd;
    portENTER_CRITICAL(&s_lock);
    hid_merge_get_keyboard(&s_merge, &kbd);
    portEXIT_CRITICAL(&s_lock);
    memcpy(buffer, &kbd, sizeof(kbd));
    return sizeof(kbd);
}

/**
 * @brief SET_REPORT control request / OUT endpoint data: the host sets the keyboard LEDs.
 *
 * @param instance    HID interface index.
 * @param report_id   Report ID (0 on the keyboard interface; some hosts put the LED byte after an ID).
 * @param report_type Must be OUTPUT.
 * @param buffer      Report payload.
 * @param bufsize     Payload size.
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void)report_id;
    if (instance != ITF_KEYBOARD || report_type != HID_REPORT_TYPE_OUTPUT || bufsize < 1) {
        return;
    }
    /* Boot keyboard output report = 1 byte of LED bits (HID 1.11 App. B.1). */
    uint8_t leds = buffer[0] & (HID_LED_NUM_LOCK | HID_LED_CAPS_LOCK | HID_LED_SCROLL_LOCK);
    s_leds = leds;
    usb_hid_leds_cb_t cb = s_leds_cb;
    if (cb != NULL) {
        cb(leds);
    }
}
