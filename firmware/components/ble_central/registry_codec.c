/**
 * @file registry_codec.c
 * @brief Decoding of the persisted device registry (see registry_codec.h).
 *
 * Dependencies: none (pure C). Threading: stateless.
 */
#include "registry_codec.h"

#include <string.h>

/** @copydoc registry_decode */
bool registry_decode(const void *blob, size_t len, reg_rec_t recs[REG_SLOTS]) {
    memset(recs, 0, sizeof(reg_rec_t) * REG_SLOTS);
    if (blob == NULL || len == 0 || len % sizeof(reg_rec_t) != 0) {
        return false;
    }
    size_t n = len / sizeof(reg_rec_t);
    if (n > REG_SLOTS) {
        n = REG_SLOTS; /* Written by a firmware with more slots: keep the ones we have. */
    }
    memcpy(recs, blob, n * sizeof(reg_rec_t));
    for (size_t i = 0; i < REG_SLOTS; i++) {
        recs[i].name[REG_NAME_MAX - 1] = '\0'; /* Never trust stored strings. */
    }
    return true;
}
