/**
 * @file ota.h
 * @brief Over-the-air update engine: streaming install, rollback, self-test, boot-failure guard.
 *
 * Responsibilities (CLAUDE.md §3.8):
 *  - Streaming install into the inactive OTA slot without buffering the image in RAM.
 *  - Reject images for another chip or another board (board marker) or with a bad checksum.
 *  - Rollback: a new image boots "pending verify" and must pass a self-test within the deadline,
 *    otherwise it is invalidated and the previous image is booted again.
 *  - Boot-failure counter: repeated boots without a passed self-test trigger a rollback.
 * Signature checks and anti-downgrade arrive in M6 (see docs/adr/0006-signing-scheme.md).
 * Dependencies: app_update, settings, wifi_ap, board, semver. Threading: the install session is
 * single-instance and used from one HTTP handler task; status/self-test functions are safe from any task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum self-test checks that can be registered. */
#define OTA_SELFTEST_MAX_CHECKS 8

/** Result of an install step. */
typedef enum {
    OTA_OK = 0,             /**< Success. */
    OTA_ERR_BUSY,           /**< Another install is running, or the running image is still pending verification. */
    OTA_ERR_BAD_SIZE,       /**< Declared size is zero or does not fit the slot. */
    OTA_ERR_BAD_IMAGE,      /**< Not an ESP32-S3 application image (magic/chip id). */
    OTA_ERR_BOARD_MISMATCH, /**< Image built for another board, or marker missing. */
    OTA_ERR_FLASH,          /**< Flash write failed. */
    OTA_ERR_VALIDATION,     /**< Checksum/hash/descriptor validation failed. */
} ota_result_t;

/** Description of the OTA state for the Update page. */
typedef struct {
    char running_slot[17];    /**< Label of the running slot ("ota_0"/"ota_1"). */
    char running_version[33]; /**< Version string of the running image. */
    char other_slot[17];      /**< Label of the other slot. */
    char other_version[33];   /**< Version in the other slot, empty if none. */
    bool pending_verify;      /**< Running image has not yet passed its self-test. */
    bool rollback_happened;   /**< The other slot holds an image that was invalidated/aborted. */
    uint32_t boot_failures;   /**< Current boot-failure counter. */
    char last_result[64];     /**< Text of the last OTA attempt, empty if none. */
} ota_status_t;

/** Progress callback: bytes received so far and the total. Called from the install task. */
typedef void (*ota_progress_cb_t)(uint32_t received, uint32_t total);

/** A self-test check: returns true when the subsystem is healthy. */
typedef bool (*ota_check_fn_t)(void);

/**
 * @brief Boot-time guard: increment the failure counter and roll back after repeated failed boots.
 *
 * Call early in app_main() after settings_init(). May reboot and not return.
 */
void ota_boot_check(void);

/**
 * @brief Fill the OTA status.
 *
 * @param out Receives the status.
 */
void ota_get_status(ota_status_t *out);

/**
 * @brief Set the progress callback used by install sessions.
 *
 * @param cb Callback, or NULL to disable.
 */
void ota_set_progress_cb(ota_progress_cb_t cb);

/**
 * @brief Start an install session.
 *
 * @param total Declared image size in bytes (Content-Length).
 * @return OTA_OK or the reason it cannot start. On success the AP is held on until finish/abort.
 */
ota_result_t ota_session_begin(size_t total);

/**
 * @brief Write the next chunk of the image.
 *
 * Validates the image header with the first bytes and scans for the board marker.
 *
 * @param data Chunk.
 * @param len  Chunk length.
 * @return OTA_OK or an error; after an error the session is aborted automatically.
 */
ota_result_t ota_session_write(const uint8_t *data, size_t len);

/**
 * @brief Finish: validate the image, check the board, select it for the next boot.
 *
 * @param version_out Receives the new image version (may be NULL).
 * @param cap         Capacity of @p version_out.
 * @return OTA_OK (the caller should reboot) or an error; the session is closed either way.
 */
ota_result_t ota_session_finish(char *version_out, size_t cap);

/** @brief Abort the running session, if any. */
void ota_session_abort(void);

/**
 * @brief Register a self-test check (before ota_selftest_start()).
 *
 * @param name  Short name for the log (static string).
 * @param check Check function.
 */
void ota_selftest_register(const char *name, ota_check_fn_t check);

/**
 * @brief Start the self-test task: when every check passes within the deadline the running image is
 *        confirmed; otherwise it is invalidated and the device reboots into the previous image.
 */
void ota_selftest_start(void);

/**
 * @brief Human readable text for an ota_result_t.
 *
 * @param r Result.
 * @return Static string.
 */
const char *ota_result_text(ota_result_t r);

/**
 * @brief Whether an install session (file upload or online download) is open.
 *
 * @return true while a session is running (used by the status LED).
 */
bool ota_session_active(void);

#ifdef __cplusplus
}
#endif
