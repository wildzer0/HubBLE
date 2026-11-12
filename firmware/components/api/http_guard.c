/**
 * @file http_guard.c
 * @brief CSRF guard implementation. See http_guard.h.
 *
 * Dependencies: libc. Threading: stateless.
 */
#include "http_guard.h"

#include <stddef.h>
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

/** @copydoc http_guard_host_ours */
bool http_guard_host_ours(const char *host) {
    if (host == NULL) {
        return false;
    }
    static const char *const names[] = {"192.168.4.1", "hubble.local"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const size_t n = strlen(names[i]);
        if (strncasecmp(host, names[i], n) != 0) {
            continue;
        }
        const char *rest = host + n;
        if (*rest == '\0') {
            return true;
        }
        if (*rest == ':' && rest[1] != '\0') {
            for (const char *d = rest + 1; *d != '\0'; d++) {
                if (*d < '0' || *d > '9') {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

/** @copydoc http_guard_ws_ok */
bool http_guard_ws_ok(const char *origin, const char *host) {
    if (!http_guard_host_ours(host)) {
        return false;
    }
    if (origin == NULL) {
        return true;
    }
    const char *h = origin;
    if (strncasecmp(h, "http://", 7) == 0) {
        h += 7;
    } else if (strncasecmp(h, "https://", 8) == 0) {
        h += 8;
    }
    return strcasecmp(h, host) == 0;
}
