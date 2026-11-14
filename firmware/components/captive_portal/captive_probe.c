/**
 * @file captive_probe.c
 * @brief Captive-portal probe classification (see captive_probe.h).
 *
 * Dependencies: libc only. Threading: stateless.
 */
#include "captive_probe.h"

#include <string.h>

/** Probe path -> kind. The hosts differ (gstatic, apple, msft...) but the paths are distinctive enough. */
static const struct {
    const char *path;     /**< Request path of the probe. */
    captive_probe_t kind; /**< Platform the probe belongs to. */
} PROBES[] = {
    {"/generate_204", CAPTIVE_PROBE_ANDROID},       {"/gen_204", CAPTIVE_PROBE_ANDROID},
    {"/hotspot-detect.html", CAPTIVE_PROBE_APPLE},  {"/library/test/success.html", CAPTIVE_PROBE_APPLE},
    {"/connecttest.txt", CAPTIVE_PROBE_MS_CONNECT}, {"/ncsi.txt", CAPTIVE_PROBE_MS_NCSI},
    {"/success.txt", CAPTIVE_PROBE_FIREFOX},        {"/check_network_status.txt", CAPTIVE_PROBE_NETWORKMGR},
};

/** @copydoc captive_probe_classify */
captive_probe_t captive_probe_classify(const char *path) {
    if (path == NULL) {
        return CAPTIVE_PROBE_NONE;
    }
    for (size_t i = 0; i < sizeof(PROBES) / sizeof(PROBES[0]); i++) {
        if (strcmp(path, PROBES[i].path) == 0) {
            return PROBES[i].kind;
        }
    }
    return CAPTIVE_PROBE_NONE;
}

/** @copydoc captive_probe_status */
int captive_probe_status(captive_probe_t p) {
    return p == CAPTIVE_PROBE_ANDROID ? 204 : 200;
}

/** @copydoc captive_probe_body */
const char *captive_probe_body(captive_probe_t p) {
    switch (p) {
    case CAPTIVE_PROBE_APPLE:
        return "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    case CAPTIVE_PROBE_MS_CONNECT:
        return "Microsoft Connect Test";
    case CAPTIVE_PROBE_MS_NCSI:
        return "Microsoft NCSI";
    case CAPTIVE_PROBE_FIREFOX:
        return "success\n";
    case CAPTIVE_PROBE_NETWORKMGR:
        return "NetworkManager is online\n";
    case CAPTIVE_PROBE_ANDROID:
    case CAPTIVE_PROBE_NONE:
    default:
        return "";
    }
}

/** @copydoc captive_probe_type */
const char *captive_probe_type(captive_probe_t p) {
    return p == CAPTIVE_PROBE_APPLE ? "text/html" : "text/plain";
}
