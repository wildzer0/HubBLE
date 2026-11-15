/**
 * @file device_registry.c
 * @brief NVS persistence of the device registry (see device_registry.h).
 *
 * Dependencies: nvs_flash. Threading: caller serialises.
 */
#include "device_registry.h"

#include <string.h>

#include "nvs.h"

/** NVS namespace and key. A fixed-size blob keeps the format trivial; a size mismatch resets the registry. */
#define REG_NS  "hubble_dev"
#define REG_KEY "registry"

/** @copydoc registry_load */
esp_err_t registry_load(reg_rec_t recs[REG_SLOTS]) {
    memset(recs, 0, sizeof(reg_rec_t) * REG_SLOTS);
    nvs_handle_t h;
    esp_err_t err = nvs_open(REG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; /* Never written: empty registry. */
    }
    if (err != ESP_OK) {
        return err;
    }
    size_t len = sizeof(reg_rec_t) * REG_SLOTS;
    reg_rec_t tmp[REG_SLOTS] = {0};
    err = nvs_get_blob(h, REG_KEY, tmp, &len);
    nvs_close(h);
    /* The registry used to hold REG_SLOTS_V1 records: accept that size too (the extra slots stay empty), otherwise
     * every device paired with an older firmware would be forgotten on update. Any other size resets the registry. */
    if (err == ESP_OK && (len == sizeof(tmp) || len == sizeof(reg_rec_t) * REG_SLOTS_V1)) {
        memcpy(recs, tmp, len);
        for (int i = 0; i < REG_SLOTS; i++) {
            recs[i].name[REG_NAME_MAX - 1] = '\0'; /* Never trust stored strings. */
        }
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

/** @copydoc registry_save */
esp_err_t registry_save(const reg_rec_t recs[REG_SLOTS]) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(REG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, REG_KEY, recs, sizeof(reg_rec_t) * REG_SLOTS);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/** @copydoc registry_find */
int registry_find(const reg_rec_t recs[REG_SLOTS], const uint8_t addr[6]) {
    for (int i = 0; i < REG_SLOTS; i++) {
        if (recs[i].used && memcmp(recs[i].addr, addr, 6) == 0) {
            return i;
        }
    }
    return -1;
}

/** @copydoc registry_free_slot */
int registry_free_slot(const reg_rec_t recs[REG_SLOTS]) {
    for (int i = 0; i < REG_SLOTS; i++) {
        if (!recs[i].used) {
            return i;
        }
    }
    return -1;
}
