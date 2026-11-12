/**
 * @file fuzz_dns_reply.c
 * @brief Fuzz target: the captive-portal DNS responder parses datagrams from anyone on the Wi-Fi.
 */
#include <stddef.h>
#include <stdint.h>

#include "dns_reply.h"

/** libFuzzer entry point. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static const uint8_t ip[4] = {192, 168, 4, 1};
    uint8_t reply[DNS_MAX_MESSAGE + 32];
    if (size > DNS_MAX_MESSAGE) {
        size = DNS_MAX_MESSAGE; /* The UDP receive buffer of the firmware is this big. */
    }
    const size_t n = dns_build_reply(data, size, ip, reply, size + 16 < sizeof(reply) ? size + 16 : sizeof(reply));
    if (n > sizeof(reply)) {
        __builtin_trap(); /* The reply must always fit the capacity that was passed. */
    }
    return 0;
}
