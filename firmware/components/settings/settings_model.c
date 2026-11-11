/**
 * @file settings_model.c
 * @brief Settings validation rules. See settings_model.h.
 *
 * Dependencies: libc. Threading: stateless.
 */
#include "settings_model.h"

#include <string.h>

/** @copydoc settings_password_valid */
bool settings_password_valid(const char *password) {
    if (password == NULL) {
        return false;
    }
    const size_t len = strlen(password);
    if (len < SETTINGS_PASSWORD_MIN_LEN || len > SETTINGS_PASSWORD_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)password[i];
        if (c < 0x20 || c > 0x7E) {
            return false; /* WPA2 passphrases are printable ASCII. */
        }
    }
    return strcmp(password, SETTINGS_DEFAULT_AP_PASSWORD) != 0;
}
