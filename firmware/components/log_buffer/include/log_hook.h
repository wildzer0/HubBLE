/**
 * @file log_hook.h
 * @brief Copies everything the ESP-IDF logger prints into a RAM ring buffer.
 *
 * Responsibilities: install a vprintf hook that tees log output into the global
 * log buffer (still printing to the console), and offer thread-safe readers.
 * Dependencies: esp_log, log_buffer. Threading: all functions are safe from any task;
 * writes use a short spinlock critical section. Release builds must never log
 * keystrokes or report payloads (CLAUDE.md §3.12), so nothing sensitive reaches the buffer.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install the logger hook. Call as early as possible in app_main().
 *
 * @return ESP_OK (the hook chains to the previous vprintf).
 */
esp_err_t log_hook_install(void);

/**
 * @brief Sequence number of the next byte to be logged.
 *
 * @return End-of-buffer sequence (start a live reader here).
 */
uint64_t log_hook_end(void);

/**
 * @brief Read buffered log text.
 *
 * @param from        Sequence to start from (older data is skipped if overwritten).
 * @param out         Destination.
 * @param cap         Destination capacity.
 * @param whole_lines Only return complete lines.
 * @param next        Receives the cursor for the next call.
 * @return Bytes copied (not NUL-terminated).
 */
size_t log_hook_read(uint64_t from, char *out, size_t cap, bool whole_lines, uint64_t *next);

#ifdef __cplusplus
}
#endif
