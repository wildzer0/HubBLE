/**
 * @file dns_reply.h
 * @brief Minimal DNS responder logic for the captive portal (pure C, host-testable).
 *
 * Answers every A/IN query with one fixed IPv4 address (the dongle) and every
 * other query type with an empty NOERROR answer, so clients quickly fall back
 * to IPv4 and open the portal page (CLAUDE.md §3.5).
 * Dependencies: libc only. Threading: stateless.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest DNS message handled (classic UDP limit, RFC 1035 §2.3.4). */
#define DNS_MAX_MESSAGE 512

/**
 * @brief Build the reply for a DNS query.
 *
 * @param query   Received datagram.
 * @param qlen    Its length.
 * @param ip      The 4 address bytes in network order to return for A queries.
 * @param reply   Output buffer.
 * @param reply_cap Output capacity (>= qlen + 16 is always enough).
 * @return Reply length, or 0 if the datagram is not a well-formed standard query
 *         with exactly one question (it is then silently dropped).
 */
size_t dns_build_reply(const uint8_t *query, size_t qlen, const uint8_t ip[4], uint8_t *reply, size_t reply_cap);

#ifdef __cplusplus
}
#endif
