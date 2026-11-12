/**
 * @file hogp_client.h
 * @brief HID-over-GATT (HOGP) client for one BLE connection: discovery, report map, subscriptions, LEDs.
 *
 * Responsibilities (CLAUDE.md §3.3): once a link is encrypted, discover the HID service (0x1812), read its
 * Report Map, read the Report Reference descriptor of every Report characteristic, subscribe to the input
 * reports (report protocol), discover/read/subscribe the Battery service (0x180F), and write keyboard LED
 * output reports. Devices that expose no usable Report Map fall back to the boot keyboard characteristics.
 * Dependencies: NimBLE host (GATT client), hid_parser, hid_normalizer.
 * Threading: hogp_start() and every GATT callback run in the NimBLE host task. hogp_find_report() and
 * hogp_write_leds() may be called from other tasks once the state is HOGP_READY (they only read fields that
 * are no longer modified after discovery). One hogp_t per connection, owned by ble_central.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum report characteristics tracked per device (input + output + boot). */
#define HOGP_MAX_CHARS 12
/** Largest Report Map kept; longer maps are truncated (the parser then reports TRUNCATED but keeps the fields). */
#define HOGP_MAP_MAX 640

/** Discovery progress. */
typedef enum {
    HOGP_IDLE = 0, /**< Not started. */
    HOGP_BUSY,     /**< Discovery in progress. */
    HOGP_READY,    /**< Subscribed and ready; reports flow. */
    HOGP_FAILED,   /**< Discovery failed; the connection should be dropped. */
} hogp_state_t;

/** Kinds of characteristic the client cares about. */
typedef enum {
    HOGP_CHR_REPORT = 0,   /**< Report (0x2A4D). */
    HOGP_CHR_BOOT_KBD_IN,  /**< Boot Keyboard Input (0x2A22). */
    HOGP_CHR_BOOT_KBD_OUT, /**< Boot Keyboard Output (0x2A32). */
} hogp_chr_kind_t;

/** One discovered characteristic. */
typedef struct {
    uint16_t val_handle; /**< Value handle. */
    uint16_t end_handle; /**< Last handle belonging to this characteristic (to bound descriptor discovery). */
    uint16_t ccc_handle; /**< Client Characteristic Configuration descriptor handle (0 if none). */
    uint16_t ref_handle; /**< Report Reference descriptor handle (0 if none). */
    uint8_t props;       /**< GATT characteristic properties. */
    uint8_t kind;        /**< hogp_chr_kind_t. */
    uint8_t report_id;   /**< Report ID from the Report Reference (0 if none). */
    uint8_t report_type; /**< Report Reference type: 1 input, 2 output, 3 feature (0 for boot characteristics). */
} hogp_chr_t;

struct hogp;

/** Callbacks from the client to its owner (all in the NimBLE host task). */
typedef struct {
    /** Discovery finished and notifications are enabled. */
    void (*on_ready)(struct hogp *h, void *ctx);
    /** Discovery failed with a NimBLE/ATT status. */
    void (*on_failed)(struct hogp *h, int status, void *ctx);
    /** Battery level (percent) read or notified. */
    void (*on_battery)(struct hogp *h, uint8_t percent, void *ctx);
} hogp_cbs_t;

/** Per-connection client state. Treat as opaque outside hogp_client.c except for the documented fields. */
typedef struct hogp {
    uint16_t conn;                   /**< NimBLE connection handle. */
    hogp_state_t state;              /**< Discovery progress. */
    hogp_cbs_t cbs;                  /**< Owner callbacks. */
    void *ctx;                       /**< Owner context passed to callbacks. */
    uint16_t svc_start;              /**< HID service start handle. */
    uint16_t svc_end;                /**< HID service end handle. */
    hogp_chr_t chrs[HOGP_MAX_CHARS]; /**< Discovered characteristics. */
    uint8_t nchrs;                   /**< Entries in @c chrs. */
    uint8_t step;                    /**< Index used by the per-characteristic phases. */
    uint8_t phase;                   /**< Internal state-machine phase. */
    uint16_t map_handle;             /**< Report Map value handle (0 if absent). */
    uint8_t map[HOGP_MAP_MAX];       /**< Raw Report Map bytes. */
    uint16_t map_len;                /**< Bytes stored in @c map. */
    hid_layout_t layout;             /**< Parsed layout (valid once state == HOGP_READY). */
    bool boot_fallback;              /**< Using the boot keyboard layout because no Report Map was usable. */
    uint16_t bas_start;              /**< Battery service start handle (0 if absent). */
    uint16_t bas_end;                /**< Battery service end handle. */
    uint16_t bas_level_handle;       /**< Battery Level value handle (0 if absent). */
    uint16_t bas_ccc_handle;         /**< Battery Level CCC handle. */
} hogp_t;

/**
 * @brief Start discovery on an encrypted connection.
 *
 * @param h    Client state (overwritten).
 * @param conn NimBLE connection handle.
 * @param cbs  Callbacks.
 * @param ctx  Owner context.
 * @return 0 on success or a NimBLE error code (state becomes HOGP_FAILED).
 */
int hogp_start(hogp_t *h, uint16_t conn, const hogp_cbs_t *cbs, void *ctx);

/**
 * @brief Look up the input report a notification belongs to.
 *
 * @param h            Client (state HOGP_READY).
 * @param attr_handle  Attribute handle of the notification.
 * @param[out] id      Receives the report ID (0 if the device uses none).
 * @return true if @p attr_handle is a subscribed input report (or the boot keyboard input).
 */
bool hogp_find_report(const hogp_t *h, uint16_t attr_handle, uint8_t *id);

/**
 * @brief Whether a notification is the Battery Level characteristic.
 *
 * @param h           Client.
 * @param attr_handle Notification handle.
 * @return true if it is the battery level.
 */
bool hogp_is_battery(const hogp_t *h, uint16_t attr_handle);

/**
 * @brief Write the keyboard LED state to the device (no-op if the device has no LED output).
 *
 * @param h    Client (state HOGP_READY).
 * @param leds hid_led_bits_t bit field.
 * @return 0 if a write was issued (or nothing to do), else a NimBLE error.
 */
int hogp_write_leds(const hogp_t *h, uint8_t leds);

/**
 * @brief Whether the device looks like a keyboard (has keyboard-page input fields).
 *
 * @param h Client (state HOGP_READY).
 * @return true if it has a keyboard input field.
 */
bool hogp_is_keyboard(const hogp_t *h);

#ifdef __cplusplus
}
#endif
