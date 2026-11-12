/**
 * @file hogp_client.c
 * @brief HOGP client state machine (see hogp_client.h).
 *
 * Spec references: HID over GATT Profile 1.0 (HOGP) §3 (service/characteristics), §4.x (report mode
 * operation); Battery Service 1.0. GATT discovery here is strictly sequential: exactly one GATT procedure
 * is in flight per connection, and each callback issues the next step (NimBLE allows one pending procedure
 * per connection).
 * Dependencies: NimBLE host, hid_parser, hid_normalizer. Threading: see hogp_client.h.
 */
#include "hogp_client.h"

#include <string.h>

#include "esp_log.h"
#include "hid_normalizer.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

static const char *TAG = "hogp";

/* 16-bit UUIDs (Bluetooth Assigned Numbers). */
#define UUID_HID_SERVICE     0x1812
#define UUID_BATTERY_SERVICE 0x180F
#define UUID_REPORT_MAP      0x2A4B
#define UUID_REPORT          0x2A4D
#define UUID_BOOT_KBD_IN     0x2A22
#define UUID_BOOT_KBD_OUT    0x2A32
#define UUID_BATTERY_LEVEL   0x2A19
#define UUID_CCC             0x2902
#define UUID_REPORT_REF      0x2908

/** Report Reference "report type" values (HOGP §3.x). */
#define REPORT_TYPE_INPUT  1
#define REPORT_TYPE_OUTPUT 2

/** CCC value enabling notifications (little endian). */
static const uint8_t CCC_NOTIFY[2] = {0x01, 0x00};

/** Internal phases of the discovery state machine. */
enum {
    PH_SVC = 0,  /**< Discovering the HID service. */
    PH_CHRS,     /**< Discovering its characteristics. */
    PH_DSCS,     /**< Discovering descriptors, one characteristic at a time. */
    PH_MAP,      /**< Reading the Report Map. */
    PH_REFS,     /**< Reading Report Reference descriptors. */
    PH_SUB,      /**< Writing CCCs. */
    PH_BAS_SVC,  /**< Discovering the Battery service. */
    PH_BAS_CHR,  /**< Discovering the Battery Level characteristic. */
    PH_BAS_DSC,  /**< Discovering the Battery Level CCC. */
    PH_BAS_READ, /**< Reading the Battery Level. */
    PH_BAS_SUB,  /**< Subscribing to the Battery Level. */
    PH_DONE,     /**< Finished. */
};

static void advance(hogp_t *h);

/**
 * @brief Abort discovery with an error and tell the owner.
 *
 * @param h      Client.
 * @param status NimBLE/ATT status.
 */
static void fail(hogp_t *h, int status) {
    if (h->state == HOGP_FAILED) {
        return;
    }
    h->state = HOGP_FAILED;
    ESP_LOGW(TAG, "conn %u: discovery failed in phase %u (status %d)", h->conn, h->phase, status);
    if (h->cbs.on_failed != NULL) {
        h->cbs.on_failed(h, status, h->ctx);
    }
}

/**
 * @brief Map a 16-bit UUID of a characteristic to the kinds we track.
 *
 * @param u16  UUID.
 * @param[out] kind Kind when tracked.
 * @return true if tracked.
 */
static bool chr_kind(uint16_t u16, uint8_t *kind) {
    switch (u16) {
    case UUID_REPORT:
        *kind = HOGP_CHR_REPORT;
        return true;
    case UUID_BOOT_KBD_IN:
        *kind = HOGP_CHR_BOOT_KBD_IN;
        return true;
    case UUID_BOOT_KBD_OUT:
        *kind = HOGP_CHR_BOOT_KBD_OUT;
        return true;
    default:
        return false;
    }
}

/**
 * @brief Return a UUID's 16-bit value, or 0 if it is not a 16-bit UUID.
 *
 * @param uuid UUID.
 * @return 16-bit value or 0.
 */
static uint16_t uuid16(const ble_uuid_any_t *uuid) {
    return uuid->u.type == BLE_UUID_TYPE_16 ? BLE_UUID16(&uuid->u)->value : 0;
}

/* ---- Phase: HID service ------------------------------------------------------------------------ */

/**
 * @brief GATT callback: primary service discovery by UUID (HID).
 */
static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && svc != NULL) {
        if (h->svc_start == 0) { /* Keep the first HID service instance. */
            h->svc_start = svc->start_handle;
            h->svc_end = svc->end_handle;
        }
    } else if (err->status == BLE_HS_EDONE) {
        if (h->svc_start == 0) {
            fail(h, BLE_HS_ENOENT);
        } else {
            h->phase = PH_CHRS;
            advance(h);
        }
    } else {
        fail(h, err->status);
    }
    return 0;
}

/* ---- Phase: characteristics --------------------------------------------------------------------- */

/**
 * @brief GATT callback: characteristic discovery inside the HID service.
 *
 * Records the interesting ones and closes the previous one's handle range when the next characteristic starts.
 */
static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && chr != NULL) {
        if (h->nchrs > 0 && h->chrs[h->nchrs - 1].end_handle == 0) {
            h->chrs[h->nchrs - 1].end_handle = (uint16_t)(chr->def_handle - 1);
        }
        uint16_t u = uuid16(&chr->uuid);
        uint8_t kind;
        if (u == UUID_REPORT_MAP) {
            h->map_handle = chr->val_handle;
        }
        if (chr_kind(u, &kind) && h->nchrs < HOGP_MAX_CHARS) {
            hogp_chr_t *c = &h->chrs[h->nchrs++];
            memset(c, 0, sizeof(*c));
            c->val_handle = chr->val_handle;
            c->props = chr->properties;
            c->kind = kind;
        }
    } else if (err->status == BLE_HS_EDONE) {
        if (h->nchrs > 0 && h->chrs[h->nchrs - 1].end_handle == 0) {
            h->chrs[h->nchrs - 1].end_handle = h->svc_end;
        }
        h->phase = PH_DSCS;
        h->step = 0;
        advance(h);
    } else {
        fail(h, err->status);
    }
    return 0;
}

/* ---- Phase: descriptors ------------------------------------------------------------------------- */

/**
 * @brief GATT callback: descriptor discovery of one HID characteristic.
 */
static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg) {
    (void)conn;
    (void)chr_val_handle;
    hogp_t *h = arg;
    if (err->status == 0 && dsc != NULL) {
        hogp_chr_t *c = &h->chrs[h->step];
        uint16_t u = uuid16(&dsc->uuid);
        if (u == UUID_CCC) {
            c->ccc_handle = dsc->handle;
        } else if (u == UUID_REPORT_REF) {
            c->ref_handle = dsc->handle;
        }
    } else if (err->status == BLE_HS_EDONE) {
        h->step++;
        advance(h);
    } else {
        fail(h, err->status);
    }
    return 0;
}

/* ---- Phase: Report Map -------------------------------------------------------------------------- */

/**
 * @brief GATT callback: Report Map read (long read, delivered in chunks).
 */
static int on_map(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && attr != NULL && attr->om != NULL) {
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (h->map_len + n > HOGP_MAP_MAX) {
            n = (uint16_t)(HOGP_MAP_MAX - h->map_len); /* Truncate: still parse what we have. */
        }
        if (n > 0 && os_mbuf_copydata(attr->om, 0, n, &h->map[h->map_len]) == 0) {
            h->map_len = (uint16_t)(h->map_len + n);
        }
    } else if (err->status == BLE_HS_EDONE || err->status == 0) {
        if (err->status == BLE_HS_EDONE) {
            h->phase = PH_REFS;
            h->step = 0;
            advance(h);
        }
    } else {
        /* Map unreadable (e.g. ATT error): continue with the boot fallback if possible. */
        ESP_LOGW(TAG, "conn %u: Report Map read failed (%d)", h->conn, err->status);
        h->map_len = 0;
        h->phase = PH_REFS;
        h->step = 0;
        advance(h);
    }
    return 0;
}

/* ---- Phase: Report References ------------------------------------------------------------------- */

/**
 * @brief GATT callback: Report Reference descriptor read (2 bytes: report ID, report type).
 */
static int on_ref(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && attr != NULL && attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 2) {
        uint8_t ref[2];
        if (os_mbuf_copydata(attr->om, 0, 2, ref) == 0) {
            h->chrs[h->step].report_id = ref[0];
            h->chrs[h->step].report_type = ref[1];
        }
    } else if (err->status != 0) {
        ESP_LOGW(TAG, "conn %u: Report Reference read failed (%d)", h->conn, err->status);
    }
    h->step++;
    advance(h);
    return 0;
}

/* ---- Phase: subscriptions ----------------------------------------------------------------------- */

/**
 * @brief GATT callback: CCC write of an input report.
 */
static int on_sub(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    (void)attr;
    hogp_t *h = arg;
    if (err->status != 0) {
        ESP_LOGW(TAG, "conn %u: subscribe failed (%d)", h->conn, err->status);
        h->chrs[h->step].ccc_handle = 0; /* Mark as not subscribed. */
    }
    h->step++;
    advance(h);
    return 0;
}

/* ---- Battery ------------------------------------------------------------------------------------ */

/** @brief GATT callback: Battery service discovery. A missing service is not an error. */
static int on_bas_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && svc != NULL) {
        if (h->bas_start == 0) {
            h->bas_start = svc->start_handle;
            h->bas_end = svc->end_handle;
        }
    } else {
        h->phase = (err->status == BLE_HS_EDONE && h->bas_start != 0) ? PH_BAS_CHR : PH_DONE;
        advance(h);
    }
    return 0;
}

/** @brief GATT callback: Battery Level characteristic discovery. */
static int on_bas_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && chr != NULL) {
        if (h->bas_level_handle == 0) { /* Keep the first Battery Level characteristic of the service. */
            h->bas_level_handle = chr->val_handle;
        }
    } else {
        h->phase = (err->status == BLE_HS_EDONE && h->bas_level_handle != 0) ? PH_BAS_DSC : PH_DONE;
        advance(h);
    }
    return 0;
}

/** @brief GATT callback: Battery Level descriptor discovery (looking for the CCC). */
static int on_bas_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                      const struct ble_gatt_dsc *dsc, void *arg) {
    (void)conn;
    (void)chr_val_handle;
    hogp_t *h = arg;
    if (err->status == 0 && dsc != NULL) {
        if (uuid16(&dsc->uuid) == UUID_CCC) {
            h->bas_ccc_handle = dsc->handle;
        }
    } else {
        h->phase = PH_BAS_READ;
        advance(h);
    }
    return 0;
}

/** @brief GATT callback: Battery Level read. */
static int on_bas_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    uint8_t level;
    if (err->status == 0 && attr != NULL && attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 1 &&
        os_mbuf_copydata(attr->om, 0, 1, &level) == 0) {
        /* Log what the device really says: a wrong percentage is either the device's own value or our reading. */
        ESP_LOGI(TAG, "conn %u: battery level characteristic 0x%04x returned %u (%u byte%s)", h->conn,
                 (unsigned)h->bas_level_handle, (unsigned)level, (unsigned)OS_MBUF_PKTLEN(attr->om),
                 OS_MBUF_PKTLEN(attr->om) == 1 ? "" : "s");
        if (level <= 100 && h->cbs.on_battery != NULL) {
            h->cbs.on_battery(h, level, h->ctx);
        }
    } else {
        ESP_LOGW(TAG, "conn %u: battery level read failed (status %d)", h->conn, err->status);
    }
    h->phase = h->bas_ccc_handle != 0 ? PH_BAS_SUB : PH_DONE;
    advance(h);
    return 0;
}

/** @brief GATT callback: periodic Battery Level re-read (see hogp_read_battery). */
static int on_bas_poll(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    uint8_t level;
    if (err->status == 0 && attr != NULL && attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 1 &&
        os_mbuf_copydata(attr->om, 0, 1, &level) == 0 && level <= 100 && h->cbs.on_battery != NULL) {
        h->cbs.on_battery(h, level, h->ctx);
    }
    return 0;
}

/** @copydoc hogp_read_battery */
void hogp_read_battery(hogp_t *h) {
    if (h->state == HOGP_READY && h->bas_level_handle != 0) {
        ble_gattc_read(h->conn, h->bas_level_handle, on_bas_poll, h); /* Busy or failed: the next period retries. */
    }
}

/** @brief GATT callback: Battery CCC write; the result does not matter. */
static int on_bas_sub(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    (void)err;
    (void)attr;
    hogp_t *h = arg;
    h->phase = PH_DONE;
    advance(h);
    return 0;
}

/* ---- Driver ------------------------------------------------------------------------------------- */

/**
 * @brief Whether a characteristic should be subscribed to for input reports.
 *
 * @param h Client.
 * @param c Characteristic.
 * @return true to subscribe.
 */
static bool wants_subscribe(const hogp_t *h, const hogp_chr_t *c) {
    if (c->ccc_handle == 0 || (c->props & BLE_GATT_CHR_PROP_NOTIFY) == 0) {
        return false;
    }
    if (c->kind == HOGP_CHR_REPORT) {
        return c->report_type == REPORT_TYPE_INPUT && !h->boot_fallback;
    }
    return c->kind == HOGP_CHR_BOOT_KBD_IN && h->boot_fallback;
}

/**
 * @brief Parse the Report Map (or select the boot keyboard layout) once the map phase is over.
 *
 * @param h Client.
 * @return true if a usable layout exists.
 */
static bool build_layout(hogp_t *h) {
    h->boot_fallback = false;
    if (h->map_len > 0) {
        hid_parse_result_t r = hid_parser_parse(h->map, h->map_len, &h->layout);
        h->parse_result = (int)r;
        ESP_LOGI(TAG, "conn %u: report map %u bytes, parse result %d, %u fields", h->conn, h->map_len, (int)r,
                 h->layout.nfields);
        if (h->layout.nfields > 0) {
            return true;
        }
    }
    for (unsigned i = 0; i < h->nchrs; i++) {
        if (h->chrs[i].kind == HOGP_CHR_BOOT_KBD_IN) {
            hid_norm_boot_keyboard_layout(&h->layout);
            h->boot_fallback = true;
            ESP_LOGW(TAG, "conn %u: no usable report map, using the boot keyboard", h->conn);
            return true;
        }
    }
    return false;
}

/**
 * @brief Issue the next GATT procedure for the current phase (the heart of the state machine).
 *
 * @param h Client.
 */
static void advance(hogp_t *h) {
    int rc = 0;
    for (;;) { /* Loop only to skip phases/characteristics that need no GATT operation. */
        switch (h->phase) {
        case PH_SVC: {
            ble_uuid16_t u = BLE_UUID16_INIT(UUID_HID_SERVICE);
            rc = ble_gattc_disc_svc_by_uuid(h->conn, &u.u, on_svc, h);
            goto issued;
        }
        case PH_CHRS:
            rc = ble_gattc_disc_all_chrs(h->conn, h->svc_start, h->svc_end, on_chr, h);
            goto issued;
        case PH_DSCS:
            while (h->step < h->nchrs && h->chrs[h->step].val_handle >= h->chrs[h->step].end_handle) {
                h->step++; /* No room for descriptors. */
            }
            if (h->step < h->nchrs) {
                const hogp_chr_t *c = &h->chrs[h->step];
                /* NimBLE wants the handle of the characteristic VALUE here (it treats it as already seen and reports
                 * only the handles after it). Passing value+1 made it discard the first descriptor of every
                 * characteristic: the CCC or the Report Reference went missing, nothing was subscribed and no input
                 * ever arrived. */
                rc = ble_gattc_disc_all_dscs(h->conn, c->val_handle, c->end_handle, on_dsc, h);
                goto issued;
            }
            h->phase = h->map_handle != 0 ? PH_MAP : PH_REFS;
            h->step = 0;
            continue;
        case PH_MAP:
            h->map_len = 0;
            rc = ble_gattc_read_long(h->conn, h->map_handle, 0, on_map, h);
            goto issued;
        case PH_REFS:
            while (h->step < h->nchrs && h->chrs[h->step].ref_handle == 0) {
                h->step++;
            }
            if (h->step < h->nchrs) {
                rc = ble_gattc_read(h->conn, h->chrs[h->step].ref_handle, on_ref, h);
                goto issued;
            }
            if (!build_layout(h)) {
                fail(h, BLE_HS_ENOENT);
                return;
            }
            h->phase = PH_SUB;
            h->step = 0;
            continue;
        case PH_SUB:
            while (h->step < h->nchrs && !wants_subscribe(h, &h->chrs[h->step])) {
                h->step++;
            }
            if (h->step < h->nchrs) {
                rc = ble_gattc_write_flat(h->conn, h->chrs[h->step].ccc_handle, CCC_NOTIFY, sizeof(CCC_NOTIFY), on_sub,
                                          h);
                goto issued;
            }
            h->phase = PH_BAS_SVC;
            continue;
        case PH_BAS_SVC: {
            ble_uuid16_t u = BLE_UUID16_INIT(UUID_BATTERY_SERVICE);
            rc = ble_gattc_disc_svc_by_uuid(h->conn, &u.u, on_bas_svc, h);
            goto issued_optional;
        }
        case PH_BAS_CHR: {
            ble_uuid16_t u = BLE_UUID16_INIT(UUID_BATTERY_LEVEL);
            rc = ble_gattc_disc_chrs_by_uuid(h->conn, h->bas_start, h->bas_end, &u.u, on_bas_chr, h);
            goto issued_optional;
        }
        case PH_BAS_DSC:
            if (h->bas_level_handle < h->bas_end) {
                rc = ble_gattc_disc_all_dscs(h->conn, h->bas_level_handle, h->bas_end, on_bas_dsc,
                                             h); /* value handle, see above */
                goto issued_optional;
            }
            h->phase = PH_BAS_READ;
            continue;
        case PH_BAS_READ:
            rc = ble_gattc_read(h->conn, h->bas_level_handle, on_bas_read, h);
            goto issued_optional;
        case PH_BAS_SUB:
            rc = ble_gattc_write_flat(h->conn, h->bas_ccc_handle, CCC_NOTIFY, sizeof(CCC_NOTIFY), on_bas_sub, h);
            goto issued_optional;
        case PH_DONE:
        default:
            h->state = HOGP_READY;
            ESP_LOGI(TAG, "conn %u: battery service 0x%04x-0x%04x, level handle 0x%04x, ccc 0x%04x", h->conn,
                     (unsigned)h->bas_start, (unsigned)h->bas_end, (unsigned)h->bas_level_handle,
                     (unsigned)h->bas_ccc_handle);
            /* One line per characteristic: enough to see why a device that reports "ready" sends no input
             * (missing Report Reference, no CCC, not an input report, subscription refused). */
            for (unsigned i = 0; i < h->nchrs; i++) {
                const hogp_chr_t *c = &h->chrs[i];
                ESP_LOGI(TAG, "conn %u: chr kind %u handle 0x%04x props 0x%02x ref id %u type %u ccc 0x%04x -> %s",
                         h->conn, (unsigned)c->kind, (unsigned)c->val_handle, (unsigned)c->props,
                         (unsigned)c->report_id, (unsigned)c->report_type, (unsigned)c->ccc_handle,
                         wants_subscribe(h, c) ? "subscribed" : "not subscribed");
            }
            ESP_LOGI(TAG, "conn %u: HID ready (%u fields%s)", h->conn, h->layout.nfields,
                     h->boot_fallback ? ", boot fallback" : "");
            if (h->cbs.on_ready != NULL) {
                h->cbs.on_ready(h, h->ctx);
            }
            return;
        }
    }
issued:
    if (rc != 0) {
        fail(h, rc);
    }
    return;
issued_optional:
    /* Battery is optional: if the procedure cannot even start, skip to the end instead of failing the device. */
    if (rc != 0) {
        h->phase = PH_DONE;
        advance(h);
    }
}

/** @copydoc hogp_start */
int hogp_start(hogp_t *h, uint16_t conn, const hogp_cbs_t *cbs, void *ctx) {
    memset(h, 0, sizeof(*h));
    h->conn = conn;
    h->cbs = *cbs;
    h->ctx = ctx;
    h->state = HOGP_BUSY;
    h->phase = PH_SVC;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &(ble_uuid16_t)BLE_UUID16_INIT(UUID_HID_SERVICE).u, on_svc, h);
    if (rc != 0) {
        h->state = HOGP_FAILED;
    }
    return rc;
}

/** @copydoc hogp_find_report */
bool hogp_find_report(const hogp_t *h, uint16_t attr_handle, uint8_t *id) {
    for (unsigned i = 0; i < h->nchrs; i++) {
        const hogp_chr_t *c = &h->chrs[i];
        if (c->val_handle == attr_handle && wants_subscribe(h, c)) {
            *id = c->kind == HOGP_CHR_REPORT ? c->report_id : 0;
            return true;
        }
    }
    return false;
}

/** @copydoc hogp_is_battery */
bool hogp_is_battery(const hogp_t *h, uint16_t attr_handle) {
    return h->bas_level_handle != 0 && h->bas_level_handle == attr_handle;
}

/** @copydoc hogp_is_keyboard */
bool hogp_is_keyboard(const hogp_t *h) {
    return hid_norm_is_keyboard(&h->layout);
}

/** @copydoc hogp_is_pointer */
bool hogp_is_pointer(const hogp_t *h) {
    return hid_norm_is_pointer(&h->layout);
}

/* ---- Identification (compatibility report) ------------------------------------------------------ */

#define UUID_GAP_SERVICE  0x1800
#define UUID_DIS_SERVICE  0x180A
#define UUID_DEVICE_NAME  0x2A00
#define UUID_APPEARANCE   0x2A01
#define UUID_MANUFACTURER 0x2A29
#define UUID_MODEL_NUMBER 0x2A24
#define UUID_FW_REVISION  0x2A26
#define UUID_PNP_ID       0x2A50

/** Identification progress. */
enum { INFO_IDLE = 0, INFO_SVCS, INFO_GAP_CHRS, INFO_DIS_CHRS, INFO_READ, INFO_DONE };
/** Which field a queued handle fills. */
enum { F_NAME = 1, F_APPEARANCE, F_MANUFACTURER, F_MODEL, F_FW_REVISION, F_PNP };

static void info_advance(hogp_t *h);

/** @brief Finish the identification read (also the exit of every failure). */
static void info_finish(hogp_t *h) {
    h->info_phase = INFO_DONE;
    h->info.done = true;
}

/** @brief GATT callback: all primary services; remember the Generic Access and Device Information ranges. */
static int on_info_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && svc != NULL) {
        if (svc->uuid.u.type == BLE_UUID_TYPE_16) {
            if (BLE_UUID16(&svc->uuid.u)->value == UUID_GAP_SERVICE) {
                h->info_gap[0] = svc->start_handle;
                h->info_gap[1] = svc->end_handle;
            } else if (BLE_UUID16(&svc->uuid.u)->value == UUID_DIS_SERVICE) {
                h->info_dis[0] = svc->start_handle;
                h->info_dis[1] = svc->end_handle;
            }
        }
    } else if (err->status == BLE_HS_EDONE) {
        h->info_phase = INFO_GAP_CHRS;
        info_advance(h);
    } else {
        info_finish(h);
    }
    return 0;
}

/** @brief Field filled by a characteristic UUID inside the given service phase (0 = not wanted). */
static uint8_t info_field_for(uint8_t phase, uint16_t u) {
    if (phase == INFO_GAP_CHRS) {
        return u == UUID_DEVICE_NAME ? F_NAME : (u == UUID_APPEARANCE ? F_APPEARANCE : 0);
    }
    switch (u) {
    case UUID_MANUFACTURER:
        return F_MANUFACTURER;
    case UUID_MODEL_NUMBER:
        return F_MODEL;
    case UUID_FW_REVISION:
        return F_FW_REVISION;
    case UUID_PNP_ID:
        return F_PNP;
    default:
        return 0;
    }
}

/** @brief GATT callback: characteristics of the Generic Access or Device Information service. */
static int on_info_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    if (err->status == 0 && chr != NULL) {
        const uint8_t field =
            chr->uuid.u.type == BLE_UUID_TYPE_16 ? info_field_for(h->info_phase, BLE_UUID16(&chr->uuid.u)->value) : 0;
        if (field != 0 && h->info_n < HOGP_INFO_READS && (chr->properties & BLE_GATT_CHR_PROP_READ) != 0) {
            h->info_handle[h->info_n] = chr->val_handle;
            h->info_field[h->info_n] = field;
            h->info_n++;
        }
    } else if (err->status == BLE_HS_EDONE) {
        h->info_phase = h->info_phase == INFO_GAP_CHRS ? INFO_DIS_CHRS : INFO_READ;
        info_advance(h);
    } else {
        info_finish(h);
    }
    return 0;
}

/**
 * @brief Copy a characteristic value into a bounded printable string.
 *
 * @param[out] dst Destination of HOGP_INFO_STR bytes.
 * @param src Raw bytes.
 * @param n   Their count.
 */
static void info_copy_string(char *dst, const uint8_t *src, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n && o < HOGP_INFO_STR - 1; i++) {
        if (src[i] >= 0x20 && src[i] < 0x7F) {
            dst[o++] = (char)src[i];
        }
    }
    dst[o] = '\0';
}

/** @brief GATT callback: one identification characteristic was read. */
static int on_info_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn;
    hogp_t *h = arg;
    uint8_t buf[24];
    if (err->status == 0 && attr != NULL && attr->om != NULL) {
        size_t n = OS_MBUF_PKTLEN(attr->om);
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        if (os_mbuf_copydata(attr->om, 0, (int)n, buf) == 0) {
            switch (h->info_field[h->info_i]) {
            case F_NAME:
                info_copy_string(h->info.device_name, buf, n);
                break;
            case F_MANUFACTURER:
                info_copy_string(h->info.manufacturer, buf, n);
                break;
            case F_MODEL:
                info_copy_string(h->info.model, buf, n);
                break;
            case F_FW_REVISION:
                info_copy_string(h->info.fw_revision, buf, n);
                break;
            case F_APPEARANCE:
                if (n >= 2) {
                    h->info.appearance = (uint16_t)(buf[0] | (buf[1] << 8));
                    h->info.has_appearance = true;
                }
                break;
            case F_PNP:
                if (n >= 7) {
                    h->info.pnp_source = buf[0];
                    h->info.pnp_vendor = (uint16_t)(buf[1] | (buf[2] << 8));
                    h->info.pnp_product = (uint16_t)(buf[3] | (buf[4] << 8));
                    h->info.pnp_version = (uint16_t)(buf[5] | (buf[6] << 8));
                    h->info.has_pnp = true;
                }
                break;
            default:
                break;
            }
        }
    }
    h->info_i++; /* An unreadable characteristic is skipped. */
    info_advance(h);
    return 0;
}

/** @brief Issue the next procedure of the identification read. */
static void info_advance(hogp_t *h) {
    int rc = 0;
    for (;;) {
        switch (h->info_phase) {
        case INFO_GAP_CHRS:
            if (h->info_gap[0] == 0) {
                h->info_phase = INFO_DIS_CHRS;
                continue;
            }
            rc = ble_gattc_disc_all_chrs(h->conn, h->info_gap[0], h->info_gap[1], on_info_chr, h);
            goto issued;
        case INFO_DIS_CHRS:
            if (h->info_dis[0] == 0) {
                h->info_phase = INFO_READ;
                continue;
            }
            rc = ble_gattc_disc_all_chrs(h->conn, h->info_dis[0], h->info_dis[1], on_info_chr, h);
            goto issued;
        case INFO_READ:
            if (h->info_i >= h->info_n) {
                info_finish(h);
                return;
            }
            rc = ble_gattc_read(h->conn, h->info_handle[h->info_i], on_info_read, h);
            goto issued;
        default:
            info_finish(h);
            return;
        }
    }
issued:
    if (rc != 0) {
        info_finish(h); /* Busy or refused: this information is optional. */
    }
}

/** @copydoc hogp_read_info */
void hogp_read_info(hogp_t *h) {
    if (h->state != HOGP_READY || h->info_phase != INFO_IDLE) {
        return;
    }
    h->info_phase = INFO_SVCS;
    if (ble_gattc_disc_all_svcs(h->conn, on_info_svc, h) != 0) {
        info_finish(h);
    }
}

/** @copydoc hogp_write_leds */
int hogp_write_leds(const hogp_t *h, uint8_t leds) {
    if (h->state != HOGP_READY) {
        return BLE_HS_ENOTCONN;
    }
    uint8_t id = 0;
    uint8_t buf[8];
    size_t n = hid_norm_build_led_report(&h->layout, leds, &id, buf, sizeof(buf));
    const hogp_chr_t *target = NULL;
    if (n > 0) {
        for (unsigned i = 0; i < h->nchrs; i++) {
            const hogp_chr_t *c = &h->chrs[i];
            if (c->kind == HOGP_CHR_REPORT && c->report_type == REPORT_TYPE_OUTPUT && c->report_id == id) {
                target = c;
                break;
            }
        }
    }
    if (target == NULL) {
        /* Boot keyboard output report: one byte of LED bits (HOGP §3.x, boot protocol mode). */
        for (unsigned i = 0; i < h->nchrs; i++) {
            if (h->chrs[i].kind == HOGP_CHR_BOOT_KBD_OUT) {
                target = &h->chrs[i];
                buf[0] = leds;
                n = 1;
                break;
            }
        }
    }
    if (target == NULL || n == 0) {
        return 0; /* Not a keyboard with LEDs: nothing to do. */
    }
    if (target->props & BLE_GATT_CHR_PROP_WRITE_NO_RSP) {
        return ble_gattc_write_no_rsp_flat(h->conn, target->val_handle, buf, (uint16_t)n);
    }
    return ble_gattc_write_flat(h->conn, target->val_handle, buf, (uint16_t)n, NULL, NULL);
}
