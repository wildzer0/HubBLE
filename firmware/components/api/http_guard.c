/**
 * @file http_guard.c
 * @brief CSRF guard implementation. See http_guard.h.
 *
 * Dependencies: libc. Threading: stateless.
 */
#include "http_guard.h"

#include <strings.h>
#include <string.h>

/** @copydoc http_guard_check */
guard_result_t http_guard_check(const char *x_hubble, const char *content_type, const char *expected_ct,
                                const char *origin, const char *host) {
    if (x_hubble == NULL || strcmp(x_hubble, "1") != 0) {
        return GUARD_ERR_HEADER;
    }
    /* Parameters such as "; charset=utf-8" may follow, so compare only the prefix. */
    if (content_type == NULL || strncasecmp(content_type, expected_ct, strlen(expected_ct)) != 0) {
        return GUARD_ERR_CONTENT_TYPE;
    }
    if (origin != NULL) {
        const char *h = origin;
        if (strncasecmp(h, "http://", 7) == 0) {
            h += 7;
        } else if (strncasecmp(h, "https://", 8) == 0) {
            h += 8;
        }
        if (host == NULL || strcasecmp(h, host) != 0) {
            return GUARD_ERR_ORIGIN;
        }
    }
    return GUARD_OK;
}
