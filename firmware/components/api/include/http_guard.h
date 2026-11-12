/**
 * @file http_guard.h
 * @brief Simple CSRF protection for state-changing API calls (pure C, host-testable).
 *
 * CLAUDE.md §3.12: every state-changing call needs the header `X-HubBLE: 1` and a
 * JSON content type (a cross-site form cannot set either), and a cross-origin
 * request is rejected. Dependencies: libc only. Threading: stateless.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Outcome of http_guard_check(). */
typedef enum {
    GUARD_OK = 0,           /**< Request allowed. */
    GUARD_ERR_HEADER,       /**< Missing or wrong `X-HubBLE` header. */
    GUARD_ERR_CONTENT_TYPE, /**< Content type is not the expected one. */
    GUARD_ERR_ORIGIN,       /**< `Origin` header present and not equal to the request host. */
} guard_result_t;

/**
 * @brief Check a state-changing request.
 *
 * @param x_hubble     Value of the `X-HubBLE` header, or NULL if absent.
 * @param content_type Value of `Content-Type`, or NULL if absent.
 * @param expected_ct  Required content type prefix, e.g. "application/json".
 * @param origin       Value of `Origin`, or NULL if absent (non-browser clients).
 * @param host         Value of `Host`, or NULL if absent.
 * @return GUARD_OK or the first failed check.
 */
guard_result_t http_guard_check(const char *x_hubble, const char *content_type, const char *expected_ct,
                                const char *origin, const char *host);

/**
 * @brief Whether a `Host` header names this dongle: the SoftAP address or `hubble.local`, optionally with a port.
 *
 * Strict (exact match, unlike the captive-portal redirect), so a page served under another name cannot talk to the API
 * even if an attacker makes that name resolve to the dongle (DNS rebinding).
 *
 * @param host Value of the `Host` header, or NULL.
 * @return true only for "192.168.4.1" or "hubble.local" (case-insensitive) with an optional ":port".
 */
bool http_guard_host_ours(const char *host);

/**
 * @brief Check the WebSocket handshake against cross-site WebSocket hijacking.
 *
 * Browsers do not apply the same-origin policy to WebSockets, so any web page could otherwise open the event stream
 * (pairing passkeys, logs, the input tester) from the visitor's browser.
 *
 * @param origin Value of `Origin`, or NULL (non-browser clients).
 * @param host   Value of `Host`, or NULL.
 * @return true if the Host names this dongle and the Origin (when present) is the same host.
 */
bool http_guard_ws_ok(const char *origin, const char *host);

#ifdef __cplusplus
}
#endif
