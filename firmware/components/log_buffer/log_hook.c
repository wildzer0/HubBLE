/**
 * @file log_hook.c
 * @brief ESP-IDF log -> ring buffer bridge. See log_hook.h.
 *
 * Responsibilities: format each log call into a stack buffer, append it to the ring under a
 * spinlock, then forward to the original vprintf so console output is unchanged.
 * Dependencies: esp_log, freertos. Threading: any task (not ISR-safe, like esp_log itself).
 */
#include "log_hook.h"

#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "log_buffer.h"
#include "sdkconfig.h"

/** Longest single log line captured; longer lines are truncated in the buffer (console is unaffected). */
#define LOG_LINE_MAX 256

/** Backing storage for the ring (internal RAM, see Kconfig help). */
static char s_store[CONFIG_HUBBLE_LOG_BUFFER_SIZE];
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
        portEXIT_CRITICAL(&s_lock);
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}

/** @copydoc log_hook_install */
esp_err_t log_hook_install(void) {
    log_buffer_init(&s_ring, s_store, sizeof(s_store));
    s_prev_vprintf = esp_log_set_vprintf(hook_vprintf);
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
