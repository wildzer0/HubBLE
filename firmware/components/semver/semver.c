/**
 * @file semver.c
 * @brief SemVer parser and comparator.
 *
 * Responsibilities: see semver.h. Dependencies: libc. Threading: stateless.
 */
#include "semver.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Parse an unsigned decimal number without leading zeros (SemVer §2).
 *
 * @param p   In/out cursor into the text.
 * @param out Receives the value.
 * @return true on success.
 */
static bool parse_number(const char **p, uint32_t *out) {
    const char *s = *p;
    if (!isdigit((unsigned char)*s)) {
        return false;
    }
    if (s[0] == '0' && isdigit((unsigned char)s[1])) {
        return false; /* Leading zeros are not allowed. */
    }
    uint64_t v = 0;
    while (isdigit((unsigned char)*s)) {
        v = v * 10U + (uint64_t)(*s - '0');
        if (v > UINT32_MAX) {
            return false;
        }
        s++;
    }
    *out = (uint32_t)v;
    *p = s;
    return true;
}

/** @copydoc semver_parse */
bool semver_parse(const char *text, semver_t *out) {
    if (text == NULL || out == NULL) {
        return false;
    }
    const char *p = text;
    if (*p == 'v') {
        p++; /* Git tags carry a leading 'v'. */
    }
    semver_t v = {0};
    if (!parse_number(&p, &v.major) || *p++ != '.' || !parse_number(&p, &v.minor) || *p++ != '.' ||
        !parse_number(&p, &v.patch)) {
        return false;
    }
    if (*p == '-') {
        p++;
        size_t len = strlen(p);
        if (len == 0 || len >= SEMVER_PRERELEASE_MAX) {
            return false;
        }
        for (size_t i = 0; i < len; i++) {
            if (!isalnum((unsigned char)p[i]) && p[i] != '.' && p[i] != '-') {
                return false;
            }
        }
        memcpy(v.prerelease, p, len + 1);
        p += len;
    }
    if (*p != '\0') {
        return false;
    }
    *out = v;
    return true;
}

/** @brief Three-way compare of unsigned integers. */
static int cmp_u32(uint32_t a, uint32_t b) {
    return (a > b) - (a < b);
}

/** @brief true if the identifier [s, s+len) is purely numeric. */
static bool is_numeric(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return len > 0;
}

/**
 * @brief Compare two pre-release strings per SemVer §11.4.
 *
 * Numeric identifiers compare numerically and rank below alphanumeric ones;
 * a longer set of identifiers wins when all preceding ones are equal.
 */
static int cmp_prerelease(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        size_t la = strcspn(a, ".");
        size_t lb = strcspn(b, ".");
        bool na = is_numeric(a, la);
        bool nb = is_numeric(b, lb);
        int c;
        if (na && nb) {
            /* Lengths are bounded by SEMVER_PRERELEASE_MAX, so strtoul on a copy is safe. */
            char ba[SEMVER_PRERELEASE_MAX], bb[SEMVER_PRERELEASE_MAX];
            memcpy(ba, a, la);
            ba[la] = '\0';
            memcpy(bb, b, lb);
            bb[lb] = '\0';
            unsigned long ua = strtoul(ba, NULL, 10);
            unsigned long ub = strtoul(bb, NULL, 10);
            c = (ua > ub) - (ua < ub);
        } else if (na != nb) {
            c = na ? -1 : 1;
        } else {
            size_t m = la < lb ? la : lb;
            c = memcmp(a, b, m);
            if (c == 0) {
                c = (la > lb) - (la < lb);
            }
        }
        if (c != 0) {
            return c < 0 ? -1 : 1;
        }
        a += la;
        b += lb;
        if (*a == '.') {
            a++;
        }
        if (*b == '.') {
            b++;
        }
    }
    return (*a != '\0') - (*b != '\0');
}

/** @copydoc semver_compare */
int semver_compare(const semver_t *a, const semver_t *b) {
    int c = cmp_u32(a->major, b->major);
    if (c == 0) {
        c = cmp_u32(a->minor, b->minor);
    }
    if (c == 0) {
        c = cmp_u32(a->patch, b->patch);
    }
    if (c != 0) {
        return c;
    }
    /* A version without pre-release ranks above any pre-release of the same triple. */
    bool pa = a->prerelease[0] != '\0';
    bool pb = b->prerelease[0] != '\0';
    if (!pa && !pb) {
        return 0;
    }
    if (!pa) {
        return 1;
    }
    if (!pb) {
        return -1;
    }
    return cmp_prerelease(a->prerelease, b->prerelease);
}
