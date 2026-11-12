/**
 * @file ble_central.h
 * @brief NimBLE central: scanning, bonding, allow-list auto-reconnect and up to 4 HID peripherals.
 *
 * Responsibilities (CLAUDE.md §3.3): own the NimBLE host; keep a persistent registry of up to
 * BLE_CENTRAL_MAX_DEVICES bonded peripherals (each with a stable slot used as the merge source index);
 * connect ONLY to bonded devices (allow-list) and reconnect automatically in the background; run a
 * pairing-candidate flow (scan -> connect -> bond) that is started explicitly (M3: simple Just Works pairing,
 * the secure flows arrive in M4); request a 7.5 ms connection interval and expose the negotiated values;
 * run the HOGP client per connection and hand decoded-ready reports to the bridge; forward host keyboard
 * LED state; collect per-device statistics.
 * Dependencies: NimBLE host, hogp_client, hid_*, lat_stats, nvs_flash, esp_timer.
 * Threading: NimBLE callbacks run in the NimBLE host task. Public functions may be called from any task; shared
 * state is protected by an internal mutex (never held while calling out to the bridge callbacks).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hid_parser.h"
#include "lat_stats.h"
#include "pairing.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum bonded peripherals connected simultaneously (CLAUDE.md §3.3). */
#define BLE_CENTRAL_MAX_DEVICES 4
/** Devices remembered in the scan list during pairing. */
#define BLE_CENTRAL_MAX_SCAN 12
/** Maximum length of a device name (including NUL). */
#define BLE_CENTRAL_NAME_MAX 32
/** Battery level value meaning "unknown". */
#define BLE_BATTERY_UNKNOWN 0xFF
/** Maximum time of the connect + pair phase (CLAUDE.md §3.4: 60 s). */
#define BLE_PAIRING_TIMEOUT_MS 60000

/** Pairing flow states (see pairing.h). */
typedef pair_state_t ble_pair_state_t;

/** One entry of the pairing scan list. */
typedef struct {
    uint8_t addr[6];                 /**< Address, most significant byte first (display order). */
    uint8_t addr_type;               /**< BLE address type (public/random). */
    int8_t rssi;                     /**< Last RSSI. */
    uint16_t appearance;             /**< GAP appearance (0 if unknown). */
    char name[BLE_CENTRAL_NAME_MAX]; /**< Advertised name ("" if none). */
} ble_scan_result_t;

/** Snapshot of one registered device, for the API. */
typedef struct {
    bool in_use;                     /**< Slot holds a registered (bonded) device. */
    uint8_t slot;                    /**< Slot index = merge source index. */
    char id[13];                     /**< Stable id: the address as 12 hex digits. */
    char name[BLE_CENTRAL_NAME_MAX]; /**< User-visible name. */
    uint8_t addr_type;               /**< BLE address type. */
    bool connected;                  /**< Link is up. */
    bool ready;                      /**< HID discovery finished: reports flow. */
    bool keyboard;                   /**< Has keyboard input fields. */
    sec_level_t security;            /**< Security badge of the bond. */
    bool boot_fallback;              /**< Using the boot keyboard layout (no usable report map). */
    uint8_t battery;                 /**< Battery percent or BLE_BATTERY_UNKNOWN. */
    int8_t rssi;                     /**< Last RSSI (0 if unknown). */
    uint16_t conn_itvl_125us;        /**< Negotiated connection interval, in 1.25 ms units. */
    uint16_t conn_latency;           /**< Negotiated peripheral latency (events). */
    uint16_t conn_timeout_10ms;      /**< Supervision timeout, in 10 ms units. */
    uint32_t reconnects;             /**< Connections since boot. */
    uint32_t last_seen_s;            /**< Uptime seconds of the last report/notification. */
    lat_summary_t latency;           /**< Notification -> USB report latency summary. */
} ble_device_info_t;

/** Callbacks to the bridge. */
typedef struct {
    /**
     * A decodable input report arrived.
     * @param slot      Source slot.
     * @param layout    The device's parsed layout.
     * @param report_id Report ID.
     * @param data      Payload (valid only during the call). Never log it.
     * @param len       Payload length.
     * @param rx_us     esp_timer timestamp taken when the notification reached the host task.
     */
    void (*on_report)(unsigned slot, const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len,
                      int64_t rx_us);
    /** A device connected (ready = HID discovery finished) or disconnected; the bridge must release held keys. */
    void (*on_link)(unsigned slot, bool connected);
    /** Something about a device or the pairing flow changed (for WebSocket events). */
    void (*on_changed)(int slot);
} ble_central_cbs_t;

/**
 * @brief Initialise NVS registry and NimBLE, and start the host task. The background reconnect starts once the
 *        controller is synchronised.
 *
 * @param cbs Callbacks (copied).
 * @return ESP_OK or an error from NimBLE/NVS.
 */
esp_err_t ble_central_init(const ble_central_cbs_t *cbs);

/**
 * @brief Whether the BLE stack is up (host synchronised); used by the OTA self-test.
 *
 * @return true once the host synced with the controller.
 */
bool ble_central_is_ready(void);

/**
 * @brief Copy the info of a slot.
 *
 * @param slot Slot index (< BLE_CENTRAL_MAX_DEVICES).
 * @param[out] out Receives the snapshot (in_use=false for an empty slot).
 * @return false if the slot index is invalid.
 */
bool ble_central_get_device(unsigned slot, ble_device_info_t *out);

/**
 * @brief Find a slot by its id string.
 *
 * @param id 12-hex-digit id.
 * @return Slot index or -1.
 */
int ble_central_find_by_id(const char *id);

/**
 * @brief Start a pairing scan: HID-looking advertisers are collected for up to @p duration_ms.
 *
 * Pauses the background reconnect while scanning. Fails if a pairing flow is already running or all slots are used.
 *
 * @param duration_ms Scan time in ms.
 * @return ESP_OK, ESP_ERR_INVALID_STATE (busy / not ready), ESP_ERR_NO_MEM (no free slot) or ESP_ERR_NOT_ALLOWED
 * (locked).
 */
esp_err_t ble_central_scan_start(uint32_t duration_ms);

/**
 * @brief Copy the scan list.
 *
 * @param[out] out Array of at least BLE_CENTRAL_MAX_SCAN entries.
 * @return Number of entries.
 */
size_t ble_central_scan_results(ble_scan_result_t *out);

/**
 * @brief Start pairing with a device from the scan list (CLAUDE.md §3.4).
 *
 * @param addr         Address, most significant byte first.
 * @param addr_type    BLE address type from the scan list.
 * @param allow_legacy Per-pairing override accepting legacy pairing (security level red).
 * @return ESP_OK, ESP_ERR_INVALID_STATE (busy), ESP_ERR_NO_MEM (no free slot), ESP_ERR_NOT_ALLOWED (locked after
 *         repeated failures; see ble_central_pairing_status().lock_remaining_s).
 */
esp_err_t ble_central_pair(const uint8_t addr[6], uint8_t addr_type, bool allow_legacy);

/**
 * @brief Cancel scanning / pairing and drop a half-paired device (and its bond).
 */
void ble_central_pair_cancel(void);

/**
 * @brief Current pairing state.
 *
 * @return One of ble_pair_state_t.
 */
ble_pair_state_t ble_central_pair_state(void);

/** Snapshot of the pairing state machine for the API / WebSocket. */
typedef struct {
    pair_state_t state;                     /**< State. */
    pair_fail_t fail;                       /**< Last failure reason (valid in PAIR_FAILED). */
    sec_level_t level;                      /**< Security level reached so far. */
    bool has_passkey;                       /**< A passkey must be typed on the keyboard now. */
    uint32_t passkey;                       /**< The passkey (only while has_passkey). */
    challenge_kind_t challenge_kind;        /**< Challenge kind (PAIR_QUARANTINE). */
    char challenge[CHALLENGE_CODE_LEN + 1]; /**< Click sequence ('L','R','M') or typed code. */
    uint8_t challenge_pos;                  /**< Inputs matched so far. */
    uint8_t challenge_len;                  /**< Inputs needed. */
    uint32_t remaining_ms;                  /**< Time left in the current phase. */
    uint32_t lock_remaining_s;              /**< Seconds of pairing lock left (0 = not locked). */
    uint32_t rx_reports;                    /**< Notifications received from the device being paired (diagnostic). */
    bool need_confirm;                      /**< This pairing needs the physical confirmation. */
    bool require_confirm;                   /**< Setting value. */
    char name[BLE_CENTRAL_NAME_MAX];        /**< Name of the device being paired. */
} ble_pairing_status_t;

/**
 * @brief Snapshot the pairing state machine.
 *
 * @param[out] out Receives the status.
 */
void ble_central_pairing_status(ble_pairing_status_t *out);

/**
 * @brief Apply the "Require physical confirmation" setting to the running state machine.
 *
 * @param enabled New value.
 */
void ble_central_set_require_confirm(bool enabled);

/**
 * @brief The physical confirmation happened (Ctrl + Alt held 3 s on a paired keyboard). Ignored unless a pairing waits
 * for it.
 */
void ble_central_physical_confirm(void);

/**
 * @brief Whether a slot is the pairing candidate in quarantine (its input must not reach the host).
 *
 * @param slot Slot index.
 * @return true if quarantined.
 */
bool ble_central_quarantined(unsigned slot);

/**
 * @brief Feed the mouse button state of the quarantined device to the click challenge.
 *
 * @param buttons hid_mouse_button_t bits currently pressed.
 */
void ble_central_challenge_buttons(uint8_t buttons);

/**
 * @brief Feed the pressed keys of the quarantined keyboard to the code challenge.
 *
 * @param keys Pressed key usages (6 entries).
 */
void ble_central_challenge_keys(const uint8_t keys[6]);

/**
 * @brief Forget a device: disconnect, delete the bond and free the slot.
 *
 * @param slot Slot index.
 * @return ESP_OK or ESP_ERR_NOT_FOUND.
 */
esp_err_t ble_central_forget(unsigned slot);

/**
 * @brief Rename a device.
 *
 * @param slot Slot index.
 * @param name New name (truncated to BLE_CENTRAL_NAME_MAX-1).
 * @return ESP_OK or ESP_ERR_NOT_FOUND.
 */
esp_err_t ble_central_rename(unsigned slot, const char *name);

/**
 * @brief Copy a device's raw Report Map (for the "export report map" feature and test fixtures).
 *
 * @param slot Slot index.
 * @param[out] out Buffer.
 * @param cap  Buffer size.
 * @return Bytes copied, 0 if unavailable (device not connected yet).
 */
size_t ble_central_get_report_map(unsigned slot, uint8_t *out, size_t cap);

/**
 * @brief Record a latency sample for a device (called by the bridge after queuing the USB report).
 *
 * @param slot Slot index.
 * @param us   Latency in microseconds.
 */
void ble_central_record_latency(unsigned slot, uint32_t us);

/**
 * @brief Forward the host's keyboard LED state to every connected keyboard.
 *
 * @param leds hid_led_bits_t bit field.
 */
void ble_central_write_leds(uint8_t leds);

/**
 * @brief Number of devices currently ready (connected and subscribed).
 *
 * @return Count.
 */
unsigned ble_central_connected_count(void);

/**
 * @brief Number of registered (bonded) devices.
 *
 * @return Count.
 */
unsigned ble_central_bonded_count(void);

/**
 * @brief Whether a paired keyboard exists (physical confirmation of an OTA install needs one; skipped otherwise).
 *
 * @return true if at least one registered device is a keyboard.
 */
bool ble_central_has_keyboard(void);

/**
 * @brief Change the connection parameters requested from every peripheral (power management, CLAUDE.md §3.9).
 *
 * The profile is remembered and used for new connections too. Connected peripherals receive a connection update at
 * once; a peripheral may refuse or take a moment to apply it. The active profile is the Kconfig one
 * (7.5 ms by default); the low-power one has a long interval and a high peripheral latency.
 *
 * @param itvl_units   Connection interval, 1.25 ms units.
 * @param latency      Peripheral latency.
 * @param timeout_10ms Supervision timeout, 10 ms units.
 */
void ble_central_set_conn_profile(uint16_t itvl_units, uint16_t latency, uint16_t timeout_10ms);

#ifdef __cplusplus
}
#endif
