/**
 * @file log_hook.c
 * @brief ESP-IDF log -> ring buffer bridge. See log_hook.h.
 *
 * Responsibilities: format each log call into a stack buffer, append it to the ring under a
 * spinlock, then forward to the original vprintf so console output is unchanged.
 * The ring lives in a no-init RAM section, so after a software reset (crash, watchdog, esp_restart, reboot from the
 * web app) the log of the previous run is still there and a reboot marker separates the two runs. A power-on or
 * brown-out reset leaves RAM undefined: the header check below then discards it.
 *
 * Dependencies: esp_log, freertos. Threading: any task (not ISR-safe, like esp_log itself).
 */
#include "log_hook.h"

#include <stdarg.h>
#include <stdio.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "log_buffer.h"
#include "sdkconfig.h"

/** Longest single log line captured; longer lines are truncated in the buffer (console is unaffected). */
#define LOG_LINE_MAX 256

/** Marks a valid header; a power-on RAM pattern is very unlikely to match it together with the other fields. */
#define KEEP_MAGIC 0x4C4F4731u

/** Header kept with the ring across software resets. */
typedef struct {
    uint32_t magic;     /**< KEEP_MAGIC when the header and ring are from a previous run. */
    uint32_t store;     /**< Address of the ring storage: differs after an update that moved it. */
    uint32_t cap;       /**< Ring capacity: differs when the configuration changed. */
    uint64_t total;     /**< Bytes ever written, i.e. the ring's write position. */
    uint64_t total_inv; /**< ~total, written together with it: a reset in the middle of an update is detected. */
} keep_t;

/** Log tag. */
static const char TAG[] = "log";

/** Reset-surviving header. */
static keep_t s_keep __NOINIT_ATTR;
/** Backing storage for the ring (internal RAM, see Kconfig help; no-init so that it survives a software reset). */
static char s_store[CONFIG_HUBBLE_LOG_BUFFER_SIZE] __NOINIT_ATTR;
/** The ring. */
static log_buffer_t s_ring;
/** Protects s_ring; critical sections are a memcpy of at most LOG_LINE_MAX bytes. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/** The vprintf that was active before installation. */
static vprintf_like_t s_prev_vprintf;

/**
 * @brief vprintf replacement: capture then forward.
 *
 * @param fmt printf format.
 * @param ap  Arguments (copied, because they are consumed twice).
 * @return Result of the forwarded vprintf.
 */
static int hook_vprintf(const char *fmt, va_list ap) {
    char line[LOG_LINE_MAX];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0) {
        const size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        portENTER_CRITICAL(&s_lock);
        log_buffer_write(&s_ring, line, len);
        s_keep.total = s_ring.total;
        s_keep.total_inv = ~s_ring.total;
        portEXIT_CRITICAL(&s_lock);
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}

/**
 * @brief Name of a reset reason, for the reboot marker.
 *
 * @param r Reset reason.
 * @return Static string.
 */
static const char *reset_name(esp_reset_reason_t r) {
    switch (r) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_SW:
        return "software restart";
    case ESP_RST_PANIC:
        return "crash (panic)";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_BROWNOUT:
        return "brown-out";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    default:
        return "other";
    }
}

/** @copydoc log_hook_install */
esp_err_t log_hook_install(void) {
    const esp_reset_reason_t reason = esp_reset_reason();
    /* After power-on or brown-out the RAM content is undefined; otherwise accept it only if the header is intact. */
    const bool keep = reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT && s_keep.magic == KEEP_MAGIC &&
                      s_keep.store == (uint32_t)(uintptr_t)s_store && s_keep.cap == sizeof(s_store) &&
                      s_keep.total_inv == ~s_keep.total;
    log_buffer_init(&s_ring, s_store, sizeof(s_store));
    if (keep) {
        s_ring.total = s_keep.total; /* Continue after the old text: the ring position is total % cap. */
    } else {
        s_keep.magic = KEEP_MAGIC;
        s_keep.store = (uint32_t)(uintptr_t)s_store;
        s_keep.cap = sizeof(s_store);
        s_keep.total = 0;
        s_keep.total_inv = ~(uint64_t)0;
    }
    s_prev_vprintf = esp_log_set_vprintf(hook_vprintf);
    ESP_LOGW(TAG, "=== boot: reset reason %d (%s)%s ===", (int)reason, reset_name(reason),
             keep ? "; the log above is from the previous run" : "");
    return ESP_OK;
}

/** @copydoc log_hook_end */
uint64_t log_hook_end(void) {
    portENTER_CRITICAL(&s_lock);
    const uint64_t e = log_buffer_end(&s_ring);
    portEXIT_CRITICAL(&s_lock);
    return e;
}

/** @copydoc log_hook_read */
size_t log_hook_read(uint64_t from, char *out, size_t cap, bool whole_lines, uint64_t *next) {
    portENTER_CRITICAL(&s_lock);
    const size_t n = log_buffer_read(&s_ring, from, out, cap, whole_lines, next);
    portEXIT_CRITICAL(&s_lock);
    return n;
}
