/**
 * @file ota.c
 * @brief OTA engine implementation. See ota.h.
 *
 * Dependencies: esp_ota_ops, esp_app_format, settings, wifi_ap, board, semver, board_marker, boot_guard.
 * Threading: the session state is a single static instance guarded by an atomic "busy" flag; callers must
 * use one session at a time from one task (the HTTP handler). The self-test runs in its own task.
 */
#include "ota.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_marker.h"
#include "boot_guard.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "semver.h"
#include "settings.h"
#include "wifi_ap.h"

static const char *TAG = "ota";

/** Project name every HubBLE image carries in its app descriptor (CMake `project(hubble)`). */
#define HUBBLE_PROJECT_NAME "hubble"
/** Self-test polling period. */
#define SELFTEST_POLL_MS 1000
/** Self-test task stack size. */
#define SELFTEST_STACK 4096
/** Self-test task priority. */
#define SELFTEST_PRIO 4
/** Minimum change in percent before a progress callback is issued (limits WebSocket traffic). */
#define PROGRESS_STEP_PERCENT 2

/** One install session. */
typedef struct {
    esp_ota_handle_t handle;                    /**< esp_ota handle. */
    const esp_partition_t *partition;           /**< Target (inactive) slot. */
    size_t total;                               /**< Declared size. */
    size_t received;                            /**< Bytes written so far. */
    uint8_t header[sizeof(esp_image_header_t)]; /**< First bytes, collected for the header check. */
    size_t header_len;                          /**< Bytes collected in header. */
    board_marker_scanner_t marker;              /**< Board marker scanner. */
    uint32_t last_percent;                      /**< Last percentage reported. */
} session_t;

static session_t s_sess; /**< The one open upload session (valid while `s_busy`). */
/** true while a session is open. */
static atomic_bool s_busy;
static ota_progress_cb_t s_progress_cb; /**< Optional progress observer (e.g. the WebSocket broadcaster). */

/** Registered self-test checks. */
static struct {
    const char *name;  /**< Check name, used in the log. */
    ota_check_fn_t fn; /**< Check function; must return true when the subsystem is healthy. */
} s_checks[OTA_SELFTEST_MAX_CHECKS];
static size_t s_check_count; /**< Number of entries used in `s_checks`. */

/** @copydoc ota_result_text */
const char *ota_result_text(ota_result_t r) {
    switch (r) {
    case OTA_OK:
        return "ok";
    case OTA_ERR_BUSY:
        return "busy";
    case OTA_ERR_BAD_SIZE:
        return "bad size";
    case OTA_ERR_BAD_IMAGE:
        return "not a valid ESP32-S3 image";
    case OTA_ERR_BOARD_MISMATCH:
        return "image is for another board";
    case OTA_ERR_FLASH:
        return "flash write failed";
    case OTA_ERR_VALIDATION:
        return "image validation failed";
    }
    return "unknown";
}

/** @copydoc ota_set_progress_cb */
void ota_set_progress_cb(ota_progress_cb_t cb) {
    s_progress_cb = cb;
}

/**
 * @brief Copy the version of the image in @p p into @p out (empty if unreadable).
 *
 * @param p Partition holding an app image.
 * @param out Receives the version string (empty if unreadable).
 * @param cap Capacity of @p out.
 */
static void partition_version(const esp_partition_t *p, char *out, size_t cap) {
    esp_app_desc_t desc;
    out[0] = '\0';
    if (p != NULL && esp_ota_get_partition_description(p, &desc) == ESP_OK) {
        strncpy(out, desc.version, cap - 1);
        out[cap - 1] = '\0';
    }
}

/** @copydoc ota_get_status */
void ota_get_status(ota_status_t *out) {
    memset(out, 0, sizeof(*out));
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    if (run != NULL) {
        strncpy(out->running_slot, run->label, sizeof(out->running_slot) - 1);
        partition_version(run, out->running_version, sizeof(out->running_version));
        esp_ota_img_states_t st;
        out->pending_verify = esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
    }
    if (other != NULL) {
        strncpy(out->other_slot, other->label, sizeof(out->other_slot) - 1);
        partition_version(other, out->other_version, sizeof(out->other_version));
        esp_ota_img_states_t st;
        if (esp_ota_get_state_partition(other, &st) == ESP_OK) {
            /* Bootloader marks a never-confirmed image ABORTED and a rejected one INVALID. */
            out->rollback_happened = (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED);
        }
    }
    out->boot_failures = settings_boot_failures();
    settings_get_ota_result(out->last_result, sizeof(out->last_result));
}

/** @copydoc ota_boot_check */
void ota_boot_check(void) {
    const uint32_t failures = settings_boot_failures_increment();
    ESP_LOGI(TAG, "boot failure counter: %u (reset when the self-test passes)", (unsigned)failures);
    if (boot_guard_decide(failures, esp_ota_check_rollback_is_possible()) == BOOT_GUARD_ROLLBACK) {
        ESP_LOGE(TAG, "too many failed boots: rolling back to the previous image");
        settings_set_ota_result("rolled back: repeated boot failures");
        settings_boot_failures_reset();
        esp_ota_mark_app_invalid_rollback_and_reboot();
    } else if (failures >= BOOT_GUARD_MAX_FAILURES) {
        ESP_LOGE(TAG, "too many failed boots but no previous image to roll back to");
    }
}

/**
 * @brief Report progress if the percentage advanced enough.
 *
 * @param force Report even if the percentage did not advance enough.
 */
static void report_progress(bool force) {
    if (s_progress_cb == NULL || s_sess.total == 0) {
        return;
    }
    const uint32_t pct = (uint32_t)((uint64_t)s_sess.received * 100U / s_sess.total);
    if (force || pct >= s_sess.last_percent + PROGRESS_STEP_PERCENT) {
        s_sess.last_percent = pct;
        s_progress_cb((uint32_t)s_sess.received, (uint32_t)s_sess.total);
    }
}

/** @brief Close the session and release the AP hold. */
static void session_close(void) {
    wifi_ap_set_ota_active(false);
    atomic_store(&s_busy, false);
}

/** @copydoc ota_session_begin */
ota_result_t ota_session_begin(size_t total) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) {
        return OTA_ERR_BUSY;
    }
    memset(&s_sess, 0, sizeof(s_sess));
    board_marker_init(&s_sess.marker);
    s_sess.partition = esp_ota_get_next_update_partition(NULL);
    if (s_sess.partition == NULL || total == 0 || total > s_sess.partition->size) {
        atomic_store(&s_busy, false);
        return OTA_ERR_BAD_SIZE;
    }
    s_sess.total = total;
    wifi_ap_set_ota_active(true);
    /* Sequential writes let IDF erase flash incrementally instead of erasing the whole slot up front, which would
     * stall the connection for seconds. esp_ota_begin() refuses while the running image is still pending verify. */
    const esp_err_t err = esp_ota_begin(s_sess.partition, OTA_WITH_SEQUENTIAL_WRITES, &s_sess.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        session_close();
        return err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE ? OTA_ERR_BUSY : OTA_ERR_FLASH;
    }
    ESP_LOGI(TAG, "OTA start: %u bytes -> %s", (unsigned)total, s_sess.partition->label);
    return OTA_OK;
}

/** @copydoc ota_session_active */
bool ota_session_active(void) {
    return atomic_load(&s_busy);
}

/** @copydoc ota_session_abort */
void ota_session_abort(void) {
    if (atomic_load(&s_busy)) {
        esp_ota_abort(s_sess.handle);
        session_close();
    }
}

/**
 * @brief Check the image header collected from the first bytes.
 *
 * @return true if it is an ESP32-S3 application image.
 */
static bool header_ok(void) {
    esp_image_header_t h;
    memcpy(&h, s_sess.header, sizeof(h));
    return h.magic == ESP_IMAGE_HEADER_MAGIC && h.chip_id == ESP_CHIP_ID_ESP32S3;
}

/** @copydoc ota_session_write */
ota_result_t ota_session_write(const uint8_t *data, size_t len) {
    if (!atomic_load(&s_busy)) {
        return OTA_ERR_BUSY;
    }
    if (s_sess.header_len < sizeof(s_sess.header)) {
        size_t take = sizeof(s_sess.header) - s_sess.header_len;
        take = take < len ? take : len;
        memcpy(s_sess.header + s_sess.header_len, data, take);
        s_sess.header_len += take;
        if (s_sess.header_len == sizeof(s_sess.header) && !header_ok()) {
            ota_session_abort();
            return OTA_ERR_BAD_IMAGE;
        }
    }
    board_marker_feed(&s_sess.marker, data, len);
    if (esp_ota_write(s_sess.handle, data, len) != ESP_OK) {
        ota_session_abort();
        return OTA_ERR_FLASH;
    }
    s_sess.received += len;
    report_progress(false);
    return OTA_OK;
}

/**
 * @brief Abort and persist a failure text.
 *
 * @param r Failure to record.
 * @return @p r, so callers can `return fail(...)`.
 */
static ota_result_t fail(ota_result_t r) {
    char text[SETTINGS_OTA_RESULT_MAX];
    snprintf(text, sizeof(text), "install failed: %s", ota_result_text(r));
    settings_set_ota_result(text);
    ESP_LOGE(TAG, "%s", text);
    session_close();
    return r;
}

/** @copydoc ota_session_finish */
ota_result_t ota_session_finish(char *version_out, size_t cap) {
    if (!atomic_load(&s_busy)) {
        return OTA_ERR_BUSY;
    }
    if (s_sess.header_len < sizeof(s_sess.header) || s_sess.received != s_sess.total) {
        esp_ota_abort(s_sess.handle);
        return fail(OTA_ERR_BAD_IMAGE);
    }
    /* esp_ota_end() verifies the image structure and its appended SHA-256, so corrupt uploads are rejected here. */
    if (esp_ota_end(s_sess.handle) != ESP_OK) {
        return fail(OTA_ERR_VALIDATION);
    }
    const char *marker = board_marker_id(&s_sess.marker);
    if (marker == NULL || strcmp(marker, board_get_info()->id) != 0) {
        ESP_LOGE(TAG, "board marker '%s' does not match '%s'", marker ? marker : "(none)", board_get_info()->id);
        return fail(OTA_ERR_BOARD_MISMATCH);
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(s_sess.partition, &desc) != ESP_OK ||
        strcmp(desc.project_name, HUBBLE_PROJECT_NAME) != 0) {
        return fail(OTA_ERR_VALIDATION);
    }
    semver_t v;
    if (!semver_parse(desc.version, &v)) {
        /* Development builds use `git describe` strings; allow them but make it visible. */
        ESP_LOGW(TAG, "new image version '%s' is not SemVer", desc.version);
    }
    if (esp_ota_set_boot_partition(s_sess.partition) != ESP_OK) {
        return fail(OTA_ERR_FLASH);
    }
    if (version_out != NULL && cap > 0) {
        strncpy(version_out, desc.version, cap - 1);
        version_out[cap - 1] = '\0';
    }
    char text[SETTINGS_OTA_RESULT_MAX];
    snprintf(text, sizeof(text), "installed %.40s, awaiting self-test", desc.version);
    settings_set_ota_result(text);
    settings_boot_failures_reset(); /* The new image starts with a clean counter. */
    report_progress(true);
    ESP_LOGI(TAG, "OTA ok: %s, reboot to apply", desc.version);
    session_close();
    return OTA_OK;
}

/** @copydoc ota_selftest_register */
void ota_selftest_register(const char *name, ota_check_fn_t check) {
    if (s_check_count < OTA_SELFTEST_MAX_CHECKS) {
        s_checks[s_check_count].name = name;
        s_checks[s_check_count].fn = check;
        s_check_count++;
    }
}

/**
 * @brief Run all checks once.
 *
 * @return true if all passed. Logs the first failing check.
 */
static bool run_checks(void) {
#ifdef CONFIG_HUBBLE_SELFTEST_FORCE_FAIL
    ESP_LOGW(TAG, "self-test forced to fail (test build)");
    return false;
#else
    for (size_t i = 0; i < s_check_count; i++) {
        if (!s_checks[i].fn()) {
            ESP_LOGD(TAG, "self-test check '%s' not ready", s_checks[i].name);
            return false;
        }
    }
    return true;
#endif
}

/**
 * @brief Self-test task body.
 *
 * @param arg Unused.
 */
static void selftest_task(void *arg) {
    (void)arg;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CONFIG_HUBBLE_SELFTEST_TIMEOUT_S * 1000U);
    while (xTaskGetTickCount() < deadline) {
        if (run_checks()) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "self-test passed, image confirmed (%s)", esp_err_to_name(err));
            settings_boot_failures_reset();
            char text[SETTINGS_OTA_RESULT_MAX];
            esp_app_desc_t desc = *esp_app_get_description();
            snprintf(text, sizeof(text), "running %.40s, self-test passed", desc.version);
            settings_set_ota_result(text);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(SELFTEST_POLL_MS));
    }
    ESP_LOGE(TAG, "self-test FAILED within %d s: rolling back", CONFIG_HUBBLE_SELFTEST_TIMEOUT_S);
    settings_set_ota_result("rolled back: self-test failed");
    /* Returns only if no previous image exists (e.g. first USB flash); then keep running and say so. */
    esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "no previous image to roll back to");
    vTaskDelete(NULL);
}

/** @copydoc ota_selftest_start */
void ota_selftest_start(void) {
    xTaskCreate(selftest_task, "selftest", SELFTEST_STACK, NULL, SELFTEST_PRIO, NULL);
}
