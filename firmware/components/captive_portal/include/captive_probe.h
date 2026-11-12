/**
 * @file captive_probe.h
 * @brief Pure classifier for the operating systems' "is there internet?" probes (host-testable).
 *
 * Phones, PCs and browsers request a well-known URL when they join a Wi-Fi network and compare the answer with a
 * fixed expectation. A different answer (our redirect) opens the captive-portal sign-in window; the expected answer
 * makes the OS treat the network as fine and stay quiet. In "silent" mode the dongle gives the expected answer so
 * that joining its Wi-Fi does not pop up a page and the phone does not drop the network for having no internet.
 * Dependencies: libc only. Threading: stateless.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Which probe a request is. */
typedef enum {
    CAPTIVE_PROBE_NONE = 0,   /**< Not a known probe: a normal request. */
    CAPTIVE_PROBE_ANDROID,    /**< Android/ChromeOS/Samsung: expects 204 with no body. */
    CAPTIVE_PROBE_APPLE,      /**< iOS/macOS: expects the "Success" HTML page. */
    CAPTIVE_PROBE_MS_CONNECT, /**< Windows 10/11 (msftconnecttest): expects "Microsoft Connect Test". */
    CAPTIVE_PROBE_MS_NCSI,    /**< Older Windows (NCSI): expects "Microsoft NCSI". */
    CAPTIVE_PROBE_FIREFOX,    /**< Firefox: expects "success\n". */
    CAPTIVE_PROBE_NETWORKMGR, /**< Linux NetworkManager: expects "NetworkManager is online\n". */
} captive_probe_t;

/**
 * @brief Classify a request path.
 *
 * @param path Request path without query string (for example "/generate_204").
 * @return The probe kind, or CAPTIVE_PROBE_NONE.
 */
captive_probe_t captive_probe_classify(const char *path);

/**
 * @brief HTTP status code of the answer the OS expects.
 *
 * @param p Probe kind (not NONE).
 * @return 204 for Android, 200 for the others.
 */
int captive_probe_status(captive_probe_t p);

/**
 * @brief Body of the answer the OS expects.
 *
 * @param p Probe kind (not NONE).
 * @return Static string, empty for Android.
 */
const char *captive_probe_body(captive_probe_t p);

/**
 * @brief Content type of the answer the OS expects.
 *
 * @param p Probe kind (not NONE).
 * @return Static MIME string.
 */
const char *captive_probe_type(captive_probe_t p);

#ifdef __cplusplus
}
#endif
