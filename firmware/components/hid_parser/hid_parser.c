/**
 * @file hid_parser.c
 * @brief HID report-descriptor parser implementation (see hid_parser.h).
 *
 * Spec reference: USB HID 1.11, §6.2.2 "Report Descriptor" (item encoding) and §5.x (main item data bits).
 * Dependencies: libc only. Threading: none (pure functions).
 */
#include "hid_parser.h"

#include <string.h>

/* Item prefix: bits 7..4 = tag, 3..2 = type, 1..0 = size code (3 means 4 bytes). */
#define ITEM_TYPE_MAIN   0
#define ITEM_TYPE_GLOBAL 1
#define ITEM_TYPE_LOCAL  2
#define ITEM_LONG        0xFE

/* Main item tags. */
#define MAIN_INPUT          0x8
#define MAIN_OUTPUT         0x9
#define MAIN_FEATURE        0xB
#define MAIN_COLLECTION     0xA
#define MAIN_END_COLLECTION 0xC

/* Global item tags. */
#define GLOBAL_USAGE_PAGE   0x0
#define GLOBAL_LOGICAL_MIN  0x1
#define GLOBAL_LOGICAL_MAX  0x2
#define GLOBAL_REPORT_SIZE  0x7
#define GLOBAL_REPORT_ID    0x8
#define GLOBAL_REPORT_COUNT 0x9
#define GLOBAL_PUSH         0xA
#define GLOBAL_POP          0xB

/* Local item tags. */
#define LOCAL_USAGE     0x0
#define LOCAL_USAGE_MIN 0x1
#define LOCAL_USAGE_MAX 0x2

/* Main item data bits (HID 1.11 §6.2.2.5). */
#define DATA_CONSTANT 0x01
#define DATA_VARIABLE 0x02
#define DATA_RELATIVE 0x04

/** Largest element size we handle (bits). */
#define MAX_ELEMENT_BITS 32
/** Largest element count of a single item (a keyboard bitmap has ~256). */
#define MAX_ITEM_COUNT 1024

/** Global item state (the part saved by Push / restored by Pop). */
typedef struct {
    uint16_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    uint8_t report_size;
    uint16_t report_count;
    uint8_t report_id;
} globals_t;

/** Local item state, reset after every main item (HID 1.11 §6.2.2.8). */
typedef struct {
    uint16_t usages[HID_PARSER_MAX_USAGES];
    uint8_t usage_count;
    bool has_range;
    uint16_t usage_min;
    uint16_t usage_max;
    bool usage_overflow; /* more usages than we can store: later elements fall back to the last stored one */
} locals_t;

/**
 * @brief Read an item's little-endian data.
 *
 * @param p    Data bytes.
 * @param size Number of bytes (0, 1, 2 or 4).
 * @return Unsigned value.
 */
static uint32_t item_unsigned(const uint8_t *p, unsigned size) {
    uint32_t v = 0;
    for (unsigned i = 0; i < size; i++) {
        v |= (uint32_t)p[i] << (8 * i);
    }
    return v;
}

/**
 * @brief Read an item's data as a signed value (sign-extended from its own size).
 *
 * @param p    Data bytes.
 * @param size Number of bytes (0, 1, 2 or 4).
 * @return Signed value.
 */
static int32_t item_signed(const uint8_t *p, unsigned size) {
    uint32_t v = item_unsigned(p, size);
    if (size == 1) {
        return (int8_t)v;
    }
    if (size == 2) {
        return (int16_t)v;
    }
    return (int32_t)v;
}

/**
 * @brief Find (or create) the bit counter of a report.
 *
 * @param l   Layout.
 * @param dir Direction.
 * @param id  Report ID.
 * @return Index into l->ids/bits, or -1 if the table is full.
 */
static int report_slot(hid_layout_t *l, unsigned dir, uint8_t id) {
    for (unsigned i = 0; i < l->n_ids[dir]; i++) {
        if (l->ids[dir][i] == id) {
            return (int)i;
        }
    }
    if (l->n_ids[dir] >= HID_PARSER_MAX_REPORT_IDS) {
        return -1;
    }
    unsigned i = l->n_ids[dir]++;
    l->ids[dir][i] = id;
    l->bits[dir][i] = 0;
    return (int)i;
}

/**
 * @brief Handle one Input/Output/Feature main item: advance the report bit counter and store the field.
 *
 * @param l      Layout being built.
 * @param g      Current globals.
 * @param loc    Current locals.
 * @param tag    MAIN_INPUT, MAIN_OUTPUT or MAIN_FEATURE.
 * @param flags  Data bits of the item.
 * @return HID_PARSE_OK or HID_PARSE_TOO_MANY.
 */
static hid_parse_result_t main_data_item(hid_layout_t *l, const globals_t *g, const locals_t *loc, unsigned tag,
                                         uint32_t flags) {
    if (tag == MAIN_FEATURE) {
        return HID_PARSE_OK; /* Feature reports are never exchanged by the dongle. */
    }
    if (g->report_size == 0 || g->report_size > MAX_ELEMENT_BITS || g->report_count == 0 ||
        g->report_count > MAX_ITEM_COUNT) {
        return HID_PARSE_OK; /* Nonsense size/count: ignore the item rather than guess a layout. */
    }
    unsigned dir = (tag == MAIN_OUTPUT) ? HID_REPORT_OUT : HID_REPORT_IN;
    int slot = report_slot(l, dir, g->report_id);
    if (slot < 0) {
        return HID_PARSE_TOO_MANY;
    }
    uint32_t bits = (uint32_t)g->report_size * g->report_count;
    uint32_t start = l->bits[dir][slot];
    if (start + bits > HID_PARSER_MAX_REPORT_BITS) {
        return HID_PARSE_TOO_MANY;
    }
    l->bits[dir][slot] = (uint16_t)(start + bits);

    if (flags & DATA_CONSTANT) {
        return HID_PARSE_OK; /* Padding: occupies bits but carries nothing. */
    }
    if (l->nfields >= HID_PARSER_MAX_FIELDS) {
        return HID_PARSE_TOO_MANY;
    }
    hid_field_t *f = &l->fields[l->nfields++];
    memset(f, 0, sizeof(*f));
    f->report_id = g->report_id;
    f->dir = (uint8_t)dir;
    f->flags = (uint8_t)(((flags & DATA_VARIABLE) ? HID_FLAG_VARIABLE : 0) |
                         ((flags & DATA_RELATIVE) ? HID_FLAG_RELATIVE : 0));
    f->bit_size = g->report_size;
    f->count = g->report_count;
    f->bit_offset = (uint16_t)start;
    f->usage_page = g->usage_page;
    f->logical_min = g->logical_min;
    f->logical_max = g->logical_max;
    if (loc->has_range && loc->usage_count == 0) {
        f->usage_min = loc->usage_min;
        f->usage_max = loc->usage_max;
    } else {
        f->usage_count = loc->usage_count;
        memcpy(f->usages, loc->usages, sizeof(f->usages));
        if (loc->has_range && loc->usage_count < HID_PARSER_MAX_USAGES) {
            /* Mixed list + range (rare): append the range's first usage so the field is not usage-less. */
            f->usages[f->usage_count++] = loc->usage_min;
        }
    }
    return HID_PARSE_OK;
}

/** @copydoc hid_parser_parse */
hid_parse_result_t hid_parser_parse(const uint8_t *desc, size_t len, hid_layout_t *layout) {
    if (layout == NULL) {
        return HID_PARSE_INVALID;
    }
    memset(layout, 0, sizeof(*layout));
    if (desc == NULL || len == 0) {
        return HID_PARSE_INVALID;
    }

    globals_t g;
    memset(&g, 0, sizeof(g));
    globals_t stack[HID_PARSER_MAX_PUSH];
    unsigned sp = 0;
    locals_t loc;
    memset(&loc, 0, sizeof(loc));
    unsigned depth = 0;
    hid_parse_result_t result = HID_PARSE_OK;

    size_t i = 0;
    while (i < len) {
        uint8_t prefix = desc[i++];
        unsigned size;
        unsigned tag;
        unsigned type;
        if (prefix == ITEM_LONG) {
            /* Long item: [0xFE][data size][long tag][data...]. Reserved by the spec; skip it whole. */
            if (i + 2 > len) {
                return HID_PARSE_TRUNCATED;
            }
            size_t dsize = desc[i];
            i += 2 + dsize;
            if (i > len) {
                return HID_PARSE_TRUNCATED;
            }
            continue;
        }
        size = prefix & 0x03;
        if (size == 3) {
            size = 4;
        }
        tag = prefix >> 4;
        type = (prefix >> 2) & 0x03;
        if (i + size > len) {
            return result == HID_PARSE_OK ? HID_PARSE_TRUNCATED : result;
        }
        const uint8_t *data = &desc[i];
        i += size;

        if (type == ITEM_TYPE_MAIN) {
            if (tag == MAIN_INPUT || tag == MAIN_OUTPUT || tag == MAIN_FEATURE) {
                hid_parse_result_t r = main_data_item(layout, &g, &loc, tag, item_unsigned(data, size));
                if (r != HID_PARSE_OK) {
                    result = r;
                    if (r == HID_PARSE_TOO_MANY) {
                        /* Keep what we have: a device with 50 fields may still work for its keyboard part. */
                    }
                }
            } else if (tag == MAIN_COLLECTION) {
                if (++depth > HID_PARSER_MAX_DEPTH) {
                    return HID_PARSE_BAD_DEPTH;
                }
            } else if (tag == MAIN_END_COLLECTION) {
                if (depth == 0) {
                    return HID_PARSE_BAD_DEPTH;
                }
                depth--;
            }
            memset(&loc, 0, sizeof(loc)); /* Local items do not survive a main item. */
        } else if (type == ITEM_TYPE_GLOBAL) {
            switch (tag) {
            case GLOBAL_USAGE_PAGE:
                g.usage_page = (uint16_t)item_unsigned(data, size);
                break;
            case GLOBAL_LOGICAL_MIN:
                g.logical_min = item_signed(data, size);
                break;
            case GLOBAL_LOGICAL_MAX:
                /* A max stored in an unsigned-looking item (e.g. 0xFF in one byte with min 0) must stay
                 * positive: only treat as signed when the minimum is negative. */
                g.logical_max = (g.logical_min < 0) ? item_signed(data, size) : (int32_t)item_unsigned(data, size);
                break;
            case GLOBAL_REPORT_SIZE:
                g.report_size = (uint8_t)(item_unsigned(data, size) > 255 ? 255 : item_unsigned(data, size));
                break;
            case GLOBAL_REPORT_ID:
                g.report_id = (uint8_t)item_unsigned(data, size);
                layout->uses_report_ids = true;
                break;
            case GLOBAL_REPORT_COUNT:
                g.report_count = (uint16_t)(item_unsigned(data, size) > 65535 ? 65535 : item_unsigned(data, size));
                break;
            case GLOBAL_PUSH:
                if (sp >= HID_PARSER_MAX_PUSH) {
                    return HID_PARSE_BAD_DEPTH;
                }
                stack[sp++] = g;
                break;
            case GLOBAL_POP:
                if (sp == 0) {
                    return HID_PARSE_BAD_DEPTH;
                }
                g = stack[--sp];
                break;
            default:
                break; /* Physical min/max, unit, exponent: irrelevant here. */
            }
        } else if (type == ITEM_TYPE_LOCAL) {
            uint32_t v = item_unsigned(data, size);
            if (size == 4) {
                /* Extended usage: high 16 bits = usage page. Honour it for the page of this item. */
                if ((v >> 16) != 0) {
                    g.usage_page = (uint16_t)(v >> 16);
                }
            }
            switch (tag) {
            case LOCAL_USAGE:
                if (loc.usage_count < HID_PARSER_MAX_USAGES) {
                    loc.usages[loc.usage_count++] = (uint16_t)v;
                } else {
                    loc.usage_overflow = true;
                }
                break;
            case LOCAL_USAGE_MIN:
                loc.has_range = true;
                loc.usage_min = (uint16_t)v;
                if (loc.usage_max < loc.usage_min) {
                    loc.usage_max = loc.usage_min;
                }
                break;
            case LOCAL_USAGE_MAX:
                loc.has_range = true;
                loc.usage_max = (uint16_t)v;
                break;
            default:
                break; /* Designator, string, delimiter: not needed. */
            }
        }
    }
    if (depth != 0 && result == HID_PARSE_OK) {
        result = HID_PARSE_BAD_DEPTH; /* Unterminated collection: layout is still usable. */
    }
    return result;
}

/** @copydoc hid_layout_report_bytes */
size_t hid_layout_report_bytes(const hid_layout_t *layout, hid_dir_t dir, uint8_t id) {
    if (layout == NULL || (unsigned)dir > 1) {
        return 0;
    }
    for (unsigned i = 0; i < layout->n_ids[dir]; i++) {
        if (layout->ids[dir][i] == id) {
            return ((size_t)layout->bits[dir][i] + 7) / 8;
        }
    }
    return 0;
}

/** @copydoc hid_field_usage */
uint16_t hid_field_usage(const hid_field_t *f, unsigned index) {
    if (f->usage_count > 0) {
        return f->usages[index < f->usage_count ? index : f->usage_count - 1u];
    }
    return (uint16_t)(f->usage_min + index);
}

/** @copydoc hid_field_value */
bool hid_field_value(const hid_field_t *f, unsigned index, const uint8_t *data, size_t len, int32_t *v) {
    if (index >= f->count || f->bit_size == 0 || f->bit_size > MAX_ELEMENT_BITS) {
        return false;
    }
    uint32_t start = (uint32_t)f->bit_offset + (uint32_t)index * f->bit_size;
    uint32_t end = start + f->bit_size;
    if (end > (uint32_t)len * 8u) {
        return false;
    }
    uint64_t raw = 0;
    for (unsigned b = 0; b < f->bit_size; b++) {
        uint32_t pos = start + b;
        if (data[pos >> 3] & (1u << (pos & 7))) {
            raw |= (uint64_t)1 << b;
        }
    }
    if (f->logical_min < 0 && f->bit_size < 32 && (raw & ((uint64_t)1 << (f->bit_size - 1)))) {
        raw |= ~(uint64_t)0 << f->bit_size; /* sign-extend */
    }
    *v = (int32_t)(uint32_t)raw;
    return true;
}
