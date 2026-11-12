/**
 * @file device_registry.h
 * @brief Persistent list of registered (bonded) peripherals, stored in NVS (private to ble_central).
 *
 * The registry holds only what the bond store does not: the slot assignment and the user-visible name.
 * Keys and bonds themselves live in NimBLE's own NVS store. No keystroke data is ever stored here.
 * Threading: not thread-safe; ble_central serialises access with its mutex.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define REG_SLOTS    4
#define REG_NAME_MAX 32

/** One registry record. */
typedef struct {
    uint8_t used;            /**< 1 when the slot is assigned. */
    uint8_t addr_type;       /**< BLE address type. */
    uint8_t sec;             /**< sec_level_t of the bond (security badge). */
    uint8_t kbd;             /**< 1 if the device is a keyboard (can confirm pairings). */
    uint8_t addr[6];         /**< Address in NimBLE order (least significant byte first). */
    char name[REG_NAME_MAX]; /**< Device name. */
} reg_rec_t;

/**
 * @brief Load the registry from NVS (empty if none).
 *
 * @param[out] recs Array of REG_SLOTS records.
 * @return ESP_OK or an NVS error (the array is zeroed in that case).
 */
esp_err_t registry_load(reg_rec_t recs[REG_SLOTS]);

/**
 * @brief Persist the registry.
 *
 * @param recs Array of REG_SLOTS records.
 * @return ESP_OK or an NVS error.
 */
esp_err_t registry_save(const reg_rec_t recs[REG_SLOTS]);

/**
 * @brief Find the slot holding an address.
 *
 * @param recs      Registry.
 * @param addr      Address (NimBLE order).
 * @return Slot index or -1.
 */
int registry_find(const reg_rec_t recs[REG_SLOTS], const uint8_t addr[6]);

/**
 * @brief Find a free slot.
 *
 * @param recs Registry.
 * @return Slot index or -1 if all are used.
 */
int registry_free_slot(const reg_rec_t recs[REG_SLOTS]);
