/**
 * @file dns_reply.c
 * @brief DNS reply builder. See dns_reply.h. Format reference: RFC 1035 §4.1.
 *
 * Dependencies: libc. Threading: stateless.
 */
#include "dns_reply.h"

#include <string.h>

/** Size of the fixed DNS header. */
#define DNS_HEADER_LEN 12U
/** Record type A (IPv4 address). */
#define DNS_TYPE_A 1U
/** Record class IN (Internet). */
#define DNS_CLASS_IN 1U
/** TTL of the answer in seconds; short so clients re-query once the AP changes. */
#define DNS_TTL_S 30U
/** Size of the answer record we append (name ptr 2 + type 2 + class 2 + ttl 4 + rdlen 2 + rdata 4). */
#define DNS_ANSWER_LEN 16U
/** Flag bits: QR (response) and AA (authoritative); RD is copied from the query, RA set. */
#define DNS_FLAGS_RESPONSE 0x8400U
#define DNS_FLAG_RD        0x0100U
#define DNS_FLAG_RA        0x0080U
/** Opcode mask inside the flags word; only standard queries (opcode 0) are served. */
#define DNS_OPCODE_MASK 0x7800U
/** QR bit: set for responses, which we must never answer. */
#define DNS_FLAG_QR 0x8000U

/**
 * @brief Read a big-endian 16-bit value.
 *
 * @param p Pointer to the first of two bytes.
 * @return The decoded value.
 */
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/** @copydoc dns_build_reply */
size_t dns_build_reply(const uint8_t *query, size_t qlen, const uint8_t ip[4], uint8_t *reply, size_t reply_cap) {
    if (qlen < DNS_HEADER_LEN || qlen > DNS_MAX_MESSAGE) {
        return 0;
    }
    const uint16_t flags = rd16(query + 2);
    if ((flags & (DNS_FLAG_QR | DNS_OPCODE_MASK)) != 0 || rd16(query + 4) != 1) {
        return 0; /* Not a standard query with exactly one question. */
    }
    /* Walk the question name: a sequence of length-prefixed labels ending in 0. Compression pointers
     * are not valid in a question, so any length byte >= 0x40 is rejected. */
    size_t off = DNS_HEADER_LEN;
    for (;;) {
        if (off >= qlen) {
            return 0;
        }
        const uint8_t l = query[off++];
        if (l == 0) {
            break;
        }
        if (l >= 0x40 || off + l > qlen) {
            return 0;
        }
        off += l;
    }
    if (off + 4 > qlen) {
        return 0; /* Missing QTYPE/QCLASS. */
    }
    const uint16_t qtype = rd16(query + off);
    const uint16_t qclass = rd16(query + off + 2);
    const size_t question_end = off + 4;
    const int answer = (qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN);
    const size_t out_len = question_end + (answer ? DNS_ANSWER_LEN : 0);
    if (out_len > reply_cap) {
        return 0;
    }

    /* Header + question are echoed back; then patch flags and counts. */
    memcpy(reply, query, question_end);
    const uint16_t rflags = (uint16_t)(DNS_FLAGS_RESPONSE | (flags & DNS_FLAG_RD) | DNS_FLAG_RA);
    reply[2] = (uint8_t)(rflags >> 8);
    reply[3] = (uint8_t)rflags;
    reply[6] = 0; /* ANCOUNT */
    reply[7] = (uint8_t)(answer ? 1 : 0);
    memset(reply + 8, 0, 4); /* NSCOUNT, ARCOUNT */
    if (answer) {
        uint8_t *a = reply + question_end;
        a[0] = 0xC0; /* Name: compression pointer to the question name at offset 12. */
        a[1] = 0x0C;
        a[2] = 0;
        a[3] = DNS_TYPE_A;
        a[4] = 0;
        a[5] = DNS_CLASS_IN;
        a[6] = (uint8_t)(DNS_TTL_S >> 24);
        a[7] = (uint8_t)(DNS_TTL_S >> 16);
        a[8] = (uint8_t)(DNS_TTL_S >> 8);
        a[9] = (uint8_t)DNS_TTL_S;
        a[10] = 0;
        a[11] = 4; /* RDLENGTH */
        memcpy(a + 12, ip, 4);
    }
    return out_len;
}
