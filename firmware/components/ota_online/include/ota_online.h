/**
 * @file ota_online.h
 * @brief "Check for updates" on GitHub and install the found release (CLAUDE.md §3.8).
 *
 * Responsibilities:
 *  - join the saved Wi-Fi network as a station (APSTA), on demand only;
 *  - find the newest release (stable or pre-release channel) with the GitHub Releases API, read its `manifest.json`;
 *  - decide with ota_manifest (board, SemVer order, minimum version, signed, anti-downgrade);
 *  - after the optional physical confirmation (Ctrl + Alt, 3 s on a paired keyboard) download the image over HTTPS,
 *    check its SHA-256 against the manifest while streaming into the OTA slot, then let esp_ota_end verify the RSA
 *    signature and select the slot.
 *
 * Dependencies: ota, ota_manifest, wifi_ap, settings, ble_central, esp_http_client, esp_crt_bundle, mbedtls.
 * Threading: one worker task per operation (check or install); the status is copied under a lock, so any task may
 * read it. Only one operation runs at a time.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ota_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/** State of the online update engine. */
typedef enum {
    OU_IDLE = 0,      /**< Nothing running, nothing known. */
    OU_CONNECTING,    /**< Joining the saved Wi-Fi network. */
    OU_CHECKING,      /**< Asking GitHub for the newest release. */
    OU_AVAILABLE,     /**< A release was found that can be installed (see `decision`). */
    OU_UP_TO_DATE,    /**< The newest release is the running version. */
    OU_AWAIT_CONFIRM, /**< Waiting for Ctrl + Alt held 3 s on a paired keyboard. */
    OU_DOWNLOADING,   /**< Streaming the image into the OTA slot. */
    OU_INSTALLED,     /**< Installed; the dongle restarts in a moment. */
    OU_ERROR          /**< The last operation failed (see `error`). */
} ou_state_t;

/** Why an operation failed. */
typedef enum {
    OUE_NONE = 0,        /**< No error. */
    OUE_NO_STA,          /**< No Wi-Fi credentials saved. */
    OUE_WIFI,            /**< Could not join the Wi-Fi network. */
    OUE_NETWORK,         /**< GitHub unreachable, TLS error or unexpected HTTP status. */
    OUE_NO_RELEASE,      /**< No release found. */
    OUE_BAD_MANIFEST,    /**< The manifest is missing or malformed. */
    OUE_NO_BOARD,        /**< The release has no image for this board. */
    OUE_UNSIGNED,        /**< The release is not signed. */
    OUE_TOO_OLD,         /**< The running version is below the release's minimum: update in steps. */
    OUE_DOWNGRADE,       /**< Older release and the downgrade was not confirmed. */
    OUE_CONFIRM_TIMEOUT, /**< The physical confirmation did not happen in time. */
    OUE_HASH,            /**< The downloaded image does not match the manifest SHA-256. */
    OUE_INSTALL,         /**< Writing/validating the image failed (includes a bad signature). */
    OUE_BUSY             /**< Another operation or an upload is running. */
} ou_error_t;

/** Snapshot of the engine for the API. */
typedef struct {
    ou_state_t state;             /**< Current state. */
    ou_error_t error;             /**< Error when state is OU_ERROR (or after a failed check with a known release). */
    bool prerelease_channel;      /**< The check used the pre-release channel. */
    bool have_release;            /**< `release` and `decision` are valid. */
    ota_release_t release;        /**< The release found for this board. */
    ota_decision_t decision;      /**< What the manifest logic says about it. */
    uint32_t received;            /**< Bytes downloaded so far. */
    uint32_t total;               /**< Bytes to download. */
    uint32_t confirm_remaining_s; /**< Seconds left to confirm while OU_AWAIT_CONFIRM. */
    char detail[120]; /**< Technical reason of the last failure (stage, error, DNS answer, heap); "" if none. */
} ou_status_t;

/**
 * @brief Register a callback invoked (from the worker task) whenever the state or the progress changes.
 *
 * @param cb Callback, or NULL.
 */
void ota_online_set_changed_cb(void (*cb)(void));

/**
 * @brief Start "check for updates" in the background.
 *
 * @param prerelease true to consider pre-releases too.
 * @return ESP_OK if started; ESP_ERR_INVALID_STATE if busy, the AP is off or an upload is running;
 *         ESP_ERR_NOT_FOUND if no Wi-Fi credentials are saved.
 */
esp_err_t ota_online_check(bool prerelease);

/**
 * @brief Install the release found by the last check.
 *
 * @param allow_downgrade true if the user explicitly confirmed installing an older version.
 * @return ESP_OK if started; ESP_ERR_INVALID_STATE if there is nothing installable or the engine is busy;
 *         ESP_ERR_NOT_ALLOWED if the release is a downgrade and @p allow_downgrade is false.
 */
esp_err_t ota_online_install(bool allow_downgrade);

/**
 * @brief Physical confirmation happened (Ctrl + Alt held 3 s). Ignored unless an install waits for it.
 */
void ota_online_physical_confirm(void);

/**
 * @brief Copy the status.
 *
 * @param out Receives the snapshot.
 */
void ota_online_get_status(ou_status_t *out);

/**
 * @brief Name of a state for the API.
 *
 * @param s State.
 * @return Static string.
 */
const char *ota_online_state_name(ou_state_t s);

/**
 * @brief Name of an error for the API.
 *
 * @param e Error.
 * @return Static string ("none" if no error).
 */
const char *ota_online_error_name(ou_error_t e);

#ifdef __cplusplus
}
#endif
