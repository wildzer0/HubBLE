/**
 * @file diag.c
 * @brief Diagnostics implementation. See diag.h.
 *
 * Dependencies: heap_caps, esp_timer, esp_ota_ops, soc RTC registers. Threading: any task.
 */
#include "diag.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

/** Microseconds per second. */
#define US_PER_S 1000000LL

/** @brief Map an esp_reset_reason_t to text. */
static const char *reset_reason_text(esp_reset_reason_t r) {
    switch (r) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_USB:
        return "usb";
    default:
        return "unknown";
    }
}

/** @brief Copy partition facts into a diag_partition_t (zeroed if @p p is NULL). */
static void fill_partition(diag_partition_t *d, const esp_partition_t *p) {
    memset(d, 0, sizeof(*d));
    if (p != NULL) {
        strncpy(d->label, p->label, sizeof(d->label) - 1);
        d->address = p->address;
        d->size = p->size;
    }
}

/** @copydoc diag_collect */
void diag_collect(diag_info_t *out) {
    out->uptime_s = (uint32_t)(esp_timer_get_time() / US_PER_S);
    out->reset_reason = reset_reason_text(esp_reset_reason());
    out->heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->heap_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->heap_largest_block = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out->psram_total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    out->psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    fill_partition(&out->running, esp_ota_get_running_partition());
    fill_partition(&out->next_update, esp_ota_get_next_update_partition(NULL));
}

/** @copydoc diag_reboot_to_download */
void diag_reboot_to_download(void) {
    /* The ROM checks RTC_CNTL_OPTION1 FORCE_DOWNLOAD_BOOT at reset; ESP-IDF's own USB console code uses the
     * same register to implement its "reboot to bootloader" feature (esp_system/port/usb_console.c). */
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
    for (;;) {
    }
}
