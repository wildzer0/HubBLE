/**
 * @file fuzz_hid_parser.c
 * @brief Fuzz target: HID report descriptor parser and report decoder (untrusted input from any BLE peripheral).
 *
 * The first byte selects a report ID, the rest is the descriptor; the same bytes are then fed as a report payload
 * to the normaliser and to the field extractor. The target must never crash, overrun or trip a sanitizer.
 */
#include <stddef.h>
#include <stdint.h>

#include "hid_normalizer.h"
#include "hid_parser.h"

/** libFuzzer entry point. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    const uint8_t report_id = data[0];
    const uint8_t *desc = data + 1;
    const size_t dlen = size - 1;

    hid_layout_t layout;
    hid_parser_parse(desc, dlen, &layout); /* The partial layout after an error must stay usable. */

    hid_norm_event_t ev;
    hid_norm_decode(&layout, report_id, desc, dlen, &ev);
    hid_norm_decode(&layout, 0, desc, dlen > 8 ? 8 : dlen, &ev);

    for (unsigned i = 0; i < layout.nfields; i++) {
        const hid_field_t *f = &layout.fields[i];
        for (unsigned k = 0; k < f->count && k < 64; k++) {
            int32_t v;
            hid_field_value(f, k, desc, dlen, &v);
            hid_field_usage(f, k);
        }
    }
    hid_layout_report_bytes(&layout, HID_REPORT_IN, report_id);
    uint8_t led[16];
    uint8_t led_id;
    hid_norm_build_led_report(&layout, 0x07, &led_id, led, sizeof(led));
    return 0;
}
