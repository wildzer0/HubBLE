/**
 * @file registry_codec.h
 * @brief Pure (host-testable) encoding rules of the persisted device registry.
 *
 * Responsibilities: define the registry record and decode a stored blob of any size, so that a registry written with a
 * different number of slots (older or newer firmware) is still read instead of being thrown away.
 * Dependencies: none. Threading: stateless.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Number of registry slots (equals BLE_CENTRAL_MAX_DEVICES). */
#define REG_SLOTS 8
/** Slots of the first registry format (up to firmware 1.3.x), still accepted when loading. */
#define REG_SLOTS_V1 4
/** Longest stored device name, including the terminating NUL. */
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
 * @brief Decode a stored registry blob.
 *
 * The blob is a plain array of reg_rec_t. It may hold fewer records than REG_SLOTS (written by older firmware: the
 * rest stays empty) or more (written by newer firmware: the extra records are ignored). A size that is not a whole
 * number of records is corrupt and rejected. Names are forced to be NUL-terminated whatever was stored.
 *
 * @param blob Stored bytes.
 * @param len  Length of @p blob in bytes.
 * @param[out] recs Receives REG_SLOTS records (always fully written).
 * @return true if the blob was valid and decoded, false if it was rejected (@p recs is then all zero).
 */
bool registry_decode(const void *blob, size_t len, reg_rec_t recs[REG_SLOTS]);
