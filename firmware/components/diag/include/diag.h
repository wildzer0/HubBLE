/**
 * @file diag.h
 * @brief System diagnostics snapshot and "reboot to USB download mode".
 *
 * Responsibilities: collect uptime, heap/PSRAM, reset reason and partition info for the
 * System page (CLAUDE.md §3.7/§3.11), and reboot into the ROM download mode.
 * Dependencies: esp_system, heap, esp_ota_ops. Threading: any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Description of one flash partition. */
typedef struct {
    char label[17];   /**< Partition label (e.g. "ota_0"). */
    uint32_t address; /**< Flash offset. */
    uint32_t size;    /**< Size in bytes. */
} diag_partition_t;

/** Snapshot of system health. */
typedef struct {
    uint32_t uptime_s;            /**< Seconds since boot. */
    const char *reset_reason;     /**< Human readable reason of the last reset (static string). */
    uint32_t heap_free;           /**< Free internal RAM in bytes. */
    uint32_t heap_min_free;       /**< Lowest free internal RAM since boot. */
    uint32_t heap_largest_block;  /**< Largest allocatable internal block. */
    uint32_t psram_total;         /**< Total PSRAM in bytes (0 if none). */
    uint32_t psram_free;          /**< Free PSRAM in bytes. */
    diag_partition_t running;     /**< Partition the firmware runs from. */
    diag_partition_t next_update; /**< Partition the next OTA would write to. */
} diag_info_t;

/**
 * @brief Fill a diagnostics snapshot.
 *
 * @param out Receives the data.
 */
void diag_collect(diag_info_t *out);

/**
 * @brief Read the on-chip temperature sensor (die temperature, typically 10-25 degrees above the board surface).
 *
 * The sensor is installed on first use. Thread-safe for the single HTTP task that calls it.
 *
 * @param[out] celsius Receives the temperature in degrees Celsius.
 * @return true if a reading is available.
 */
bool diag_chip_temperature(float *celsius);

/**
 * @brief Reboot into the ROM USB download mode so a USB re-flash needs no BOOT button.
 *
 * Sets the RTC "force download boot" flag and restarts; does not return.
 * Callers should call usb_hid_shutdown() first so TinyUSB releases the PHY (M2: TinyUSB owns it at runtime).
 */
void diag_reboot_to_download(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
