/**
 * @file ble_central.c
 * @brief NimBLE central implementation (see ble_central.h).
 *
 * Design:
 *  - One `dev_t` per registry slot (0..3). The slot index is the merge source index of the USB bridge.
 *  - Exactly one outgoing connection attempt exists at a time: either the BACKGROUND one (allow-list
 *    connect: the controller connects to whichever registered, bonded device advertises first) or the
 *    PAIRING one (explicit address). reconcile() decides which, after every event that could change it.
 *  - Pairing scan and background connect are mutually exclusive (a controller cannot scan and initiate with
 *    different filters at once), so the background connect is paused while pairing.
 *  - HOGP discovery starts only after the link is encrypted; unknown devices never get that far because the
 *    controller only connects to allow-listed (registered + bonded) addresses, except for the one explicit
 *    pairing candidate.
 * Dependencies: NimBLE host, hogp_client, hid_*, lat_stats, device_registry, esp_timer, FreeRTOS.
 * Threading: see ble_central.h. Lock order: s_mu is never held while the bridge callbacks run.
 */
#include "ble_central.h"

#include <stdio.h>
#include <string.h>

#include "device_registry.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hid_normalizer.h"
#include "hogp_client.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "pairing.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"
#include "settings.h"
/* Declared by no public header in ESP-IDF; ESP-IDF's own NimBLE examples declare it the same way. */
void ble_store_config_init(void);

static const char *TAG = "ble";

/** `arg` values distinguishing the two kinds of outgoing connection in GAP events. */
#define ARG_BACKGROUND ((void *)1)
#define ARG_PAIRING    ((void *)2)
/** Handle value meaning "no connection". */
#define NO_CONN 0xFFFF
/** Delay before re-arming the background connect after a link ended (avoids a tight loop on a failing device). */
#define RECONNECT_DELAY_MS 1000
/** Period of the pairing tick (timeouts, lock expiry). */
#define PAIR_TICK_US (1 * 1000000)
/** Period of the RSSI / parameter refresh. */
#define STATS_PERIOD_US (2 * 1000000)
/** HID-over-GATT service UUID (for scan filtering) and GAP appearances in the HID range. */
#define UUID16_HID           0x1812
#define APPEARANCE_HID_FIRST 0x03C0
#define APPEARANCE_HID_LAST  0x03CF
/** Scan: interval/window in 0.625 ms units (full duty while pairing). */
#define SCAN_ITVL   0x50
#define SCAN_WINDOW 0x30
/** Background reconnect scan interval while another device is connected: 30 ms in 200 ms (15 % duty). */
#define SCAN_ITVL_BUSY 0x140
/** Retries and spacing when starting a scan right after cancelling the background connect. */
#define SCAN_RETRIES  10
#define SCAN_RETRY_MS 20
/** Largest report copied out of a notification. */
#define MAX_REPORT_COPY 96

/** Runtime state of one slot. */
typedef struct {
    uint16_t conn;        /**< Connection handle or NO_CONN. */
    bool pending;         /**< Reserved for the pairing in progress (not yet persisted). */
    bool ready;           /**< HOGP ready. */
    uint8_t battery;      /**< Percent or BLE_BATTERY_UNKNOWN. */
    int8_t rssi;          /**< Last RSSI. */
    uint16_t itvl;        /**< Connection interval, 1.25 ms units. */
    uint16_t latency;     /**< Peripheral latency. */
    uint16_t timeout;     /**< Supervision timeout, 10 ms units. */
    uint32_t reconnects;  /**< Link count since boot. */
    uint32_t last_seen_s; /**< Uptime seconds of the last report. */
    lat_stats_t lat;      /**< Latency statistics. */
    hogp_t hogp;          /**< HOGP client (valid while connected). */
} dev_t;

/** Pairing flow state. */
typedef struct {
    ble_pair_state_t state;                       /**< Current state. */
    ble_addr_t addr;                              /**< Candidate address (NimBLE order). */
    int slot;                                     /**< Reserved slot or -1. */
    bool connect_requested;                       /**< Waiting for the background connect to stop before connecting. */
    ble_scan_result_t scan[BLE_CENTRAL_MAX_SCAN]; /**< Scan list. */
    size_t nscan;                                 /**< Entries in the scan list. */
    uint16_t appearance;                          /**< GAP appearance of the candidate (0 if not advertised). */
    uint32_t rx_reports;                          /**< Notifications received from the candidate (diagnostic). */
} pair_t;

static SemaphoreHandle_t s_mu;               /**< Protects everything below. */
static reg_rec_t s_reg[REG_SLOTS];           /**< Persistent registry (pending slots are filled but not saved). */
static dev_t s_dev[BLE_CENTRAL_MAX_DEVICES]; /**< Runtime state per slot. */
static pair_t s_pair;                        /**< Pairing flow bookkeeping (address, slot, scan list). */
static pairing_t s_fsm;                      /**< Pairing state machine (pure, host-tested). */
static ble_central_cbs_t s_cbs;              /**< Bridge callbacks. */
static volatile bool s_synced;               /**< Host synchronised with the controller. */
static bool s_bg_active;                     /**< A background connect is in progress. */
static uint8_t s_own_addr_type;              /**< Own address type from ble_hs_id_infer_auto(). */
static volatile uint8_t s_leds;              /**< Last keyboard LED state from the host. */
/** Connection parameters currently requested (7.5 ms profile until the power task asks otherwise). */
static struct {
    uint16_t itvl;
    uint16_t latency;
    uint16_t timeout;
} s_profile = {CONFIG_HUBBLE_BLE_CONN_ITVL_UNITS, CONFIG_HUBBLE_BLE_CONN_LATENCY,
               CONFIG_HUBBLE_BLE_SUPERVISION_TIMEOUT_10MS};
static esp_timer_handle_t s_pair_timer;      /**< 1 s tick driving the pairing timeouts and the lock. */
static esp_timer_handle_t s_reconnect_timer; /**< Delayed reconcile after a disconnect. */
static esp_timer_handle_t s_stats_timer;     /**< Periodic RSSI/parameter refresh. */

static void reconcile(void);
static int gap_event(struct ble_gap_event *event, void *arg);

#define LOCK()   xSemaphoreTake(s_mu, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mu)

/**
 * @brief Format an address (NimBLE order) as 12 hex digits in display order (most significant first).
 *
 * @param addr Address (LSB first).
 * @param[out] out 13-byte buffer.
 */
static void addr_to_id(const uint8_t addr[6], char out[13]) {
    for (int i = 0; i < 6; i++) {
        snprintf(&out[i * 2], 3, "%02x", addr[5 - i]);
    }
}

/**
 * @brief Notify the bridge that a slot (or the pairing flow, slot -1) changed. Must be called without s_mu held.
 *
 * @param slot Slot or -1.
 */
static void changed(int slot) {
    if (s_cbs.on_changed != NULL) {
        s_cbs.on_changed(slot);
    }
}

/**
 * @brief Persist the registry without the pending (unconfirmed) slot. Caller holds s_mu.
 */
static void save_registry(void) {
    reg_rec_t tmp[REG_SLOTS];
    memcpy(tmp, s_reg, sizeof(tmp));
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        if (s_dev[i].pending) {
            memset(&tmp[i], 0, sizeof(tmp[i]));
        }
    }
    esp_err_t err = registry_save(tmp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "registry save failed: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Find the slot of a connection handle. Caller holds s_mu.
 *
 * @param conn Connection handle.
 * @return Slot or -1.
 */
static int slot_by_conn(uint16_t conn) {
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        if (s_dev[i].conn == conn) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Connection parameters requested for every link (7.5 ms interval by default, CLAUDE.md §3.3).
 *
 * @param[out] p Receives the parameters.
 */
static void conn_params(struct ble_gap_conn_params *p) {
    memset(p, 0, sizeof(*p));
    /* The connect procedure scans for the peripheral. While another device is already connected (and we are not
     * pairing) a full-duty scan would steal radio time from its 7.5 ms connection events and from the Wi-Fi AP, so
     * it is thinned out; reading the flags without the lock is fine for this heuristic. */
    bool connected = false;
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        connected = connected || (s_dev[i].ready && !s_dev[i].pending);
    }
    p->scan_itvl = (connected && !pairing_active(&s_fsm)) ? SCAN_ITVL_BUSY : SCAN_ITVL;
    p->scan_window = SCAN_WINDOW;
    p->itvl_min = s_profile.itvl;
    p->itvl_max = s_profile.itvl;
    p->latency = s_profile.latency;
    p->supervision_timeout = s_profile.timeout;
}

/**
 * @brief Refresh the cached link parameters of a slot from the controller. Caller holds s_mu.
 *
 * @param slot Slot.
 */
static void refresh_link(int slot) {
    struct ble_gap_conn_desc d;
    if (s_dev[slot].conn != NO_CONN && ble_gap_conn_find(s_dev[slot].conn, &d) == 0) {
        s_dev[slot].itvl = d.conn_itvl;
        s_dev[slot].latency = d.conn_latency;
        s_dev[slot].timeout = d.supervision_timeout;
    }
}

/* ---- Pairing helpers ---------------------------------------------------------------------------- */

/** @brief Monotonic milliseconds for the pairing state machine. */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/** @brief Hardware RNG adapter for pair_rng_t. */
static uint32_t hw_rng(void) {
    return esp_random();
}

/**
 * @brief Act on the state machine's outcome: commit a finished pairing or drop a failed one.
 *
 * Called after every event that may have moved s_fsm to BONDED or FAILED. Caller must NOT hold s_mu. Also
 * announces the change to listeners and re-arms the background connect.
 */
static void pair_sync(void) {
    LOCK();
    pair_state_t st = s_fsm.state;
    int slot = s_pair.slot;
    if (slot < 0 || (st != PAIR_BONDED && st != PAIR_FAILED)) {
        UNLOCK();
        changed(-1);
        return;
    }
    ble_addr_t addr = s_pair.addr;
    uint16_t conn = s_dev[slot].conn;
    dev_t *d = &s_dev[slot];
    d->pending = false;
    if (st == PAIR_BONDED) {
        s_reg[slot].sec = (uint8_t)s_fsm.level;
        s_reg[slot].kbd = hogp_is_keyboard(&d->hogp) ? 1 : 0;
        save_registry();
        ESP_LOGI(TAG, "pairing done, slot %d, security level %d", slot, (int)s_fsm.level);
    } else {
        ESP_LOGW(TAG, "pairing failed (reason %d)", (int)s_fsm.fail);
        memset(&s_reg[slot], 0, sizeof(s_reg[0]));
        d->ready = false;
    }
    s_pair.slot = -1;
    s_pair.connect_requested = false;
    UNLOCK();
    if (st == PAIR_FAILED) {
        if (conn != NO_CONN) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            ble_gap_conn_cancel();
        }
        ble_store_util_delete_peer(&addr); /* Never keep a bond of a pairing that did not complete. */
    } else if (s_cbs.on_link != NULL) {
        s_cbs.on_link((unsigned)slot, true);
        hogp_write_leds(&s_dev[slot].hogp, s_leds);
    }
    changed(slot);
    changed(-1);
    reconcile();
}

/**
 * @brief Report a pairing failure to the state machine and clean up.
 *
 * @param reason Failure reason.
 */
static void pair_fail(pair_fail_t reason) {
    LOCK();
    pairing_fail(&s_fsm, now_ms(), reason);
    UNLOCK();
    pair_sync();
}

/** @brief esp_timer callback (1 s): pairing phase timeouts and lock expiry. */
static void pair_tick_cb(void *arg) {
    (void)arg;
    LOCK();
    pair_state_t before = s_fsm.state;
    pairing_tick(&s_fsm, now_ms());
    bool moved = s_fsm.state != before;
    UNLOCK();
    if (moved) {
        pair_sync();
    }
}

/* ---- HOGP callbacks ----------------------------------------------------------------------------- */

/** How many notifications of a device being paired are described in the log (attribute handle and length only). */
#define PAIR_DIAG_NOTIFICATIONS 12
/** Remaining diagnostic log lines; refilled when a pairing candidate becomes ready. Touched under the lock. */
static int s_pair_diag_left;

/** @brief HOGP ready: the device now delivers reports (a pairing candidate goes on to quarantine/confirm). */
static void hogp_ready(hogp_t *h, void *ctx) {
    (void)h;
    int slot = (int)(intptr_t)ctx;
    LOCK();
    s_dev[slot].ready = true;
    bool was_pending = s_dev[slot].pending;
    if (was_pending) {
        s_pair_diag_left = PAIR_DIAG_NOTIFICATIONS;
    }
    refresh_link(slot);
    if (!was_pending) {
        /* The "keyboard" flag is stored with the bond. Older firmware classified some mice (those with a shortcut-key
         * collection) as keyboards, so re-evaluate it on every connection: a stale flag would make a new pairing wait
         * for a physical confirmation from a keyboard that does not exist. */
        const uint8_t kbd = hogp_is_keyboard(&s_dev[slot].hogp) ? 1 : 0;
        if (s_reg[slot].used && s_reg[slot].kbd != kbd) {
            s_reg[slot].kbd = kbd;
            save_registry();
        }
    }
    if (was_pending) {
        /* The advertised appearance decides, then the report map: many mice also declare a keyboard collection
         * (shortcut keys) and many keyboards also declare a mouse, so neither alone is reliable. */
        const bool kbd_challenge = hid_norm_pairing_is_keyboard(s_pair.appearance, &s_dev[slot].hogp.layout);
        pairing_on_hid_ready(&s_fsm, now_ms(), kbd_challenge, hw_rng);
    }
    UNLOCK();
    if (was_pending) {
        pair_sync(); /* Commits if the machine reached BONDED; otherwise just announces QUARANTINE/CONFIRM. */
        return;
    }
    /* Make the keyboard's lock LEDs match the host immediately. */
    hogp_write_leds(&s_dev[slot].hogp, s_leds);
    if (s_cbs.on_link != NULL) {
        s_cbs.on_link((unsigned)slot, true);
    }
    changed(slot);
}

/** @brief HOGP failed: drop the link (the pairing, if any, fails with it). */
static void hogp_failed(hogp_t *h, int status, void *ctx) {
    (void)h;
    int slot = (int)(intptr_t)ctx;
    ESP_LOGW(TAG, "slot %d: HID discovery failed (%d), disconnecting", slot, status);
    LOCK();
    uint16_t conn = s_dev[slot].conn;
    bool pending = s_dev[slot].pending;
    UNLOCK();
    if (pending) {
        pair_fail(FAIL_NO_HID);
    } else if (conn != NO_CONN) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

/** @brief HOGP battery level update. */
static void hogp_battery(hogp_t *h, uint8_t percent, void *ctx) {
    (void)h;
    int slot = (int)(intptr_t)ctx;
    LOCK();
    s_dev[slot].battery = percent;
    UNLOCK();
    changed(slot);
}

static const hogp_cbs_t s_hogp_cbs = {.on_ready = hogp_ready, .on_failed = hogp_failed, .on_battery = hogp_battery};

/* ---- GAP ---------------------------------------------------------------------------------------- */

/**
 * @brief Handle a new connection: map it to a slot, request parameters, start encryption.
 *
 * @param conn_handle Connection handle.
 * @param arg         ARG_BACKGROUND or ARG_PAIRING.
 */
static void on_connected(uint16_t conn_handle, void *arg) {
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn_handle, &d) != 0) {
        return;
    }
    LOCK();
    int slot = registry_find(s_reg, d.peer_id_addr.val);
    if (slot < 0 || (arg == ARG_BACKGROUND && s_dev[slot].pending)) {
        UNLOCK();
        ESP_LOGW(TAG, "connection from an unregistered device refused");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM); /* Allow-list only (CLAUDE.md §3.3). */
        return;
    }
    dev_t *dv = &s_dev[slot];
    dv->conn = conn_handle;
    dv->ready = false;
    dv->reconnects++;
    dv->battery = BLE_BATTERY_UNKNOWN;
    s_bg_active = (arg == ARG_BACKGROUND) ? false : s_bg_active;
    if (arg == ARG_PAIRING) {
        pairing_on_connected(&s_fsm, now_ms());
    }
    refresh_link(slot);
    UNLOCK();

    /* Ask for the target interval (the central may set it directly), then encrypt. For a bonded device this
     * re-uses the stored LTK; for the pairing candidate it starts bonding. */
    struct ble_gap_upd_params up = {.itvl_min = s_profile.itvl,
                                    .itvl_max = s_profile.itvl,
                                    .latency = s_profile.latency,
                                    .supervision_timeout = s_profile.timeout};
    ble_gap_update_params(conn_handle, &up);
    int rc = ble_gap_security_initiate(conn_handle);
    if (rc != 0) {
        ESP_LOGW(TAG, "slot %d: security initiate failed (%d)", slot, rc);
    }
    changed(slot);
    /* Another registered device may still be waiting: re-arm the background connect. */
    reconcile();
}

/**
 * @brief Handle an ended link.
 *
 * @param conn_handle Connection handle.
 */
static void on_disconnected(uint16_t conn_handle) {
    LOCK();
    int slot = slot_by_conn(conn_handle);
    bool pending = false;
    if (slot >= 0) {
        pending = s_dev[slot].pending;
        s_dev[slot].conn = NO_CONN;
        s_dev[slot].ready = false;
        s_dev[slot].hogp.state = HOGP_IDLE; /* A new link may reuse this connection handle. */
        s_dev[slot].rssi = 0;
    }
    UNLOCK();
    if (slot < 0) {
        return;
    }
    ESP_LOGI(TAG, "slot %d disconnected", slot);
    if (s_cbs.on_link != NULL) {
        s_cbs.on_link((unsigned)slot, false); /* Releases any key the device was holding. */
    }
    if (pending) {
        pair_fail(FAIL_SECURITY);
    }
    changed(slot);
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_MS * 1000);
}

/**
 * @brief Handle the result of link encryption.
 *
 * @param conn_handle Connection handle.
 * @param status      0 on success.
 */
static void on_encrypted(uint16_t conn_handle, int status) {
    struct ble_gap_conn_desc d;
    LOCK();
    int slot = slot_by_conn(conn_handle);
    bool pending = slot >= 0 && s_dev[slot].pending;
    UNLOCK();
    if (slot < 0) {
        return;
    }
    if (status != 0 || ble_gap_conn_find(conn_handle, &d) != 0 || !d.sec_state.encrypted) {
        ESP_LOGW(TAG, "slot %d: encryption failed (%d)", slot, status);
        if (pending) {
            pair_fail(FAIL_SECURITY);
        } else {
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return;
    }
    if (!pending && !d.sec_state.bonded) {
        ESP_LOGW(TAG, "slot %d: link is not bonded, refusing", slot);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    ESP_LOGI(TAG, "slot %d: encrypted (authenticated=%d, bonded=%d)", slot, d.sec_state.authenticated,
             d.sec_state.bonded);
    if (pending) {
        /* Was LE Secure Connections used? The stored peer security record knows (legacy keys have sc=0). */
        struct ble_store_key_sec key;
        struct ble_store_value_sec val;
        memset(&key, 0, sizeof(key));
        key.peer_addr = d.peer_id_addr;
        bool sc = ble_store_read_peer_sec(&key, &val) == 0 && val.sc;
        LOCK();
        bool ok = pairing_on_secured(&s_fsm, now_ms(), d.sec_state.authenticated, sc);
        UNLOCK();
        if (!ok) {
            pair_sync(); /* Legacy refused by policy: drop the device and its bond. */
            return;
        }
        changed(-1);
    }
    LOCK();
    const bool running = s_dev[slot].hogp.conn == conn_handle &&
                         (s_dev[slot].hogp.state == HOGP_BUSY || s_dev[slot].hogp.state == HOGP_READY);
    UNLOCK();
    if (running) {
        /* Some peripherals re-encrypt right after pairing. Restarting discovery from scratch in the middle of the
         * first run would wipe its state and leave the device "ready" without a single subscription. */
        ESP_LOGI(TAG, "slot %d: encryption changed again, HID discovery already running", slot);
        return;
    }
    int rc = hogp_start(&s_dev[slot].hogp, conn_handle, &s_hogp_cbs, (void *)(intptr_t)slot);
    if (rc != 0) {
        hogp_failed(&s_dev[slot].hogp, rc, (void *)(intptr_t)slot);
    }
}

/**
 * @brief Handle a notification: decode-ready report -> bridge, battery -> state.
 *
 * Timestamp first: the latency measurement starts when the host task got the data (CLAUDE.md §3.11).
 *
 * @param ev Event.
 */
static void on_notify(struct ble_gap_event *ev) {
    int64_t rx_us = esp_timer_get_time();
    LOCK();
    int slot = slot_by_conn(ev->notify_rx.conn_handle);
    if (slot < 0 || !s_dev[slot].ready) {
        UNLOCK();
        return;
    }
    dev_t *dv = &s_dev[slot];
    if (dv->pending) {
        s_pair.rx_reports++;
    }
    const bool diag = dv->pending && s_pair_diag_left > 0;
    if (diag) {
        /* Never the payload (CLAUDE.md §3.12): only which attribute spoke and how long the report was. */
        uint8_t probe = 0;
        s_pair_diag_left--;
        ESP_LOGI(TAG, "pairing candidate notification: handle 0x%04x, %u bytes, %s",
                 (unsigned)ev->notify_rx.attr_handle, (unsigned)OS_MBUF_PKTLEN(ev->notify_rx.om),
                 hogp_find_report(&dv->hogp, ev->notify_rx.attr_handle, &probe) ? "subscribed input report"
                                                                                : "NOT a subscribed input report");
    }
    uint8_t id = 0;
    uint8_t buf[MAX_REPORT_COPY];
    uint16_t len = OS_MBUF_PKTLEN(ev->notify_rx.om);
    if (hogp_is_battery(&dv->hogp, ev->notify_rx.attr_handle)) {
        uint8_t level;
        if (len >= 1 && os_mbuf_copydata(ev->notify_rx.om, 0, 1, &level) == 0 && level <= 100) {
            dv->battery = level;
        }
        UNLOCK();
        return;
    }
    if (!hogp_find_report(&dv->hogp, ev->notify_rx.attr_handle, &id) || len == 0 || len > sizeof(buf) ||
        os_mbuf_copydata(ev->notify_rx.om, 0, len, buf) != 0) {
        UNLOCK();
        return;
    }
    dv->last_seen_s = (uint32_t)(rx_us / 1000000);
    const hid_layout_t *layout = &dv->hogp.layout;
    UNLOCK();
    /* The layout is immutable while the device is ready, so using it after unlocking is safe: it is only
     * rewritten by hogp_start() on a new connection, which cannot run concurrently with this event (same task). */
    if (s_cbs.on_report != NULL) {
        s_cbs.on_report((unsigned)slot, layout, id, buf, len, rx_us);
    }
}

/**
 * @brief GAP event handler for connections (background and pairing).
 *
 * @param event Event.
 * @param arg   ARG_BACKGROUND or ARG_PAIRING.
 * @return 0 (or a BLE_GAP_REPEAT_PAIRING_* code).
 */
static int gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            on_connected(event->connect.conn_handle, arg);
        } else {
            ESP_LOGI(TAG, "connect attempt ended (status %d)", event->connect.status);
            LOCK();
            if (arg == ARG_BACKGROUND) {
                s_bg_active = false;
            }
            bool pairing_attempt = (arg == ARG_PAIRING);
            UNLOCK();
            if (pairing_attempt) {
                pair_fail(FAIL_SECURITY);
            } else {
                reconcile();
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        on_disconnected(event->disconnect.conn.conn_handle);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        on_encrypted(event->enc_change.conn_handle, event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        on_notify(event);
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE: {
        LOCK();
        int slot = slot_by_conn(event->conn_update.conn_handle);
        if (slot >= 0) {
            refresh_link(slot);
        }
        UNLOCK();
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* The peer forgot our bond and wants to pair again. Only allowed while the user is pairing. */
        struct ble_gap_conn_desc d;
        LOCK();
        bool pairing = s_fsm.state == PAIR_PAIRING;
        UNLOCK();
        if (pairing && ble_gap_conn_find(event->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        ble_gap_terminate(event->repeat_pairing.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        /* IO capability is DisplayOnly (CLAUDE.md §3.4): the only action we can serve is "display this passkey".
         * The 6-digit number comes from the hardware RNG and is shown in the web app; the user types it on the
         * keyboard. Any other action (we have no input) means the pairing cannot be authenticated: refuse it. */
        uint16_t ch = event->passkey.conn_handle;
        LOCK();
        uint32_t pk = (event->passkey.params.action == BLE_SM_IOACT_DISP) ? pairing_make_passkey(&s_fsm, hw_rng) : 0;
        bool shown = s_fsm.has_passkey;
        UNLOCK();
        if (!shown) {
            ESP_LOGW(TAG, "unsupported or unexpected passkey action %d", event->passkey.params.action);
            ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = pk};
        int rc = ble_sm_inject_io(ch, &io);
        if (rc != 0) {
            ESP_LOGW(TAG, "passkey injection failed (%d)", rc);
        }
        changed(-1); /* Tell the web app to show the passkey. */
        return 0;
    }
    default:
        return 0;
    }
}

/**
 * @brief Pairing-scan event handler: collects HID-looking advertisers.
 *
 * @param event Event.
 * @param arg   Unused.
 * @return 0.
 */
static int scan_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        LOCK();
        pairing_scan_done(&s_fsm);
        UNLOCK();
        changed(-1);
        reconcile();
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, event->disc.data, event->disc.length_data) != 0) {
        return 0;
    }
    bool hid = f.appearance_is_present && f.appearance >= APPEARANCE_HID_FIRST && f.appearance <= APPEARANCE_HID_LAST;
    for (int i = 0; !hid && i < f.num_uuids16; i++) {
        hid = f.uuids16[i].value == UUID16_HID;
    }
    if (!hid) {
        return 0;
    }
    LOCK();
    ble_scan_result_t *r = NULL;
    for (size_t i = 0; i < s_pair.nscan; i++) {
        bool same = true;
        for (int b = 0; b < 6; b++) {
            same = same && s_pair.scan[i].addr[b] == event->disc.addr.val[5 - b];
        }
        if (same) {
            r = &s_pair.scan[i];
            break;
        }
    }
    if (r == NULL && s_pair.nscan < BLE_CENTRAL_MAX_SCAN) {
        r = &s_pair.scan[s_pair.nscan++];
        memset(r, 0, sizeof(*r));
        for (int b = 0; b < 6; b++) {
            r->addr[b] = event->disc.addr.val[5 - b];
        }
        r->addr_type = event->disc.addr.type;
    }
    if (r != NULL) {
        r->rssi = event->disc.rssi;
        if (f.appearance_is_present) {
            r->appearance = f.appearance;
        }
        if (f.name != NULL &&
            f.name_len > 0) { /* The name may only be in the scan response: keep the latest non-empty. */
            size_t n = f.name_len < BLE_CENTRAL_NAME_MAX - 1 ? f.name_len : BLE_CENTRAL_NAME_MAX - 1;
            memcpy(r->name, f.name, n);
            r->name[n] = '\0';
        }
    }
    UNLOCK();
    return 0;
}

/**
 * @brief Decide what the single outgoing connection attempt should be, and start it. Safe to call any time.
 *
 * Priority: a requested pairing connect > nothing while scanning > background allow-list connect.
 */
static void reconcile(void) {
    if (!s_synced) {
        return;
    }
    struct ble_gap_conn_params p;
    conn_params(&p);

    LOCK();
    /* 1. A pairing connect was requested. */
    if (s_pair.connect_requested) {
        if (s_bg_active) {
            UNLOCK();
            ble_gap_conn_cancel(); /* The CONNECT-failed event re-enters reconcile(). */
            return;
        }
        s_pair.connect_requested = false;
        ble_addr_t addr = s_pair.addr;
        UNLOCK();
        int rc = ble_gap_connect(s_own_addr_type, &addr, BLE_PAIRING_TIMEOUT_MS, &p, gap_event, ARG_PAIRING);
        if (rc != 0) {
            ESP_LOGW(TAG, "pairing connect failed to start (%d)", rc);
            pair_fail(FAIL_SECURITY);
        }
        return;
    }
    /* 2. Scanning or pairing in progress: no background connect. */
    if (s_fsm.state == PAIR_SCANNING || s_fsm.state == PAIR_CONNECTING || s_fsm.state == PAIR_PAIRING || s_bg_active) {
        UNLOCK();
        return;
    }
    /* 3. Background: allow-list = registered, bonded, currently disconnected devices. */
    ble_addr_t bonded[BLE_CENTRAL_MAX_DEVICES + 4];
    int nbonded = 0;
    ble_store_util_bonded_peers(bonded, &nbonded, (int)(sizeof(bonded) / sizeof(bonded[0])));
    ble_addr_t wl[BLE_CENTRAL_MAX_DEVICES];
    uint8_t nwl = 0;
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        if (!s_reg[i].used || s_dev[i].pending || s_dev[i].conn != NO_CONN) {
            continue;
        }
        for (int b = 0; b < nbonded; b++) {
            if (memcmp(bonded[b].val, s_reg[i].addr, 6) == 0) {
                wl[nwl].type = s_reg[i].addr_type;
                memcpy(wl[nwl].val, s_reg[i].addr, 6);
                nwl++;
                break;
            }
        }
    }
    if (nwl == 0) {
        UNLOCK();
        return;
    }
    s_bg_active = true;
    UNLOCK();
    int rc = ble_gap_wl_set(wl, nwl);
    if (rc == 0) {
        /* peer_addr == NULL: the controller connects to any allow-listed device that advertises. */
        rc = ble_gap_connect(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, ARG_BACKGROUND);
    }
    if (rc != 0) {
        LOCK();
        s_bg_active = false;
        UNLOCK();
        ESP_LOGW(TAG, "background connect not started (%d)", rc);
    }
}

/** @brief esp_timer callback: re-run reconcile() after the post-disconnect delay. */
static void reconnect_timer_cb(void *arg) {
    (void)arg;
    reconcile();
}

/** @brief esp_timer callback: refresh RSSI and link parameters of the connected devices. */
static void stats_timer_cb(void *arg) {
    (void)arg;
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        LOCK();
        uint16_t conn = s_dev[i].conn;
        UNLOCK();
        if (conn == NO_CONN) {
            continue;
        }
        int8_t rssi;
        if (ble_gap_conn_rssi(conn, &rssi) == 0) {
            LOCK();
            s_dev[i].rssi = rssi;
            refresh_link(i);
            UNLOCK();
        }
    }
}

/* ---- Host lifecycle ----------------------------------------------------------------------------- */

/** @brief NimBLE reset callback. */
static void on_reset(int reason) {
    ESP_LOGE(TAG, "host reset, reason %d", reason);
    s_synced = false;
}

/** @brief NimBLE sync callback: the controller is ready, start the background connect. */
static void on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable address (%d)", rc);
        return;
    }
    s_synced = true;
    ESP_LOGI(TAG, "BLE host synced");
    reconcile();
}

/** @brief NimBLE host task body. */
static void host_task(void *param) {
    (void)param;
    nimble_port_run(); /* Returns only when nimble_port_stop() is called. */
    nimble_port_freertos_deinit();
}

/** @copydoc ble_central_init */
esp_err_t ble_central_init(const ble_central_cbs_t *cbs) {
    s_cbs = *cbs;
    s_mu = xSemaphoreCreateMutex();
    if (s_mu == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        s_dev[i].conn = NO_CONN;
        s_dev[i].battery = BLE_BATTERY_UNKNOWN;
        lat_stats_reset(&s_dev[i].lat);
    }
    s_pair.slot = -1;
    esp_err_t err = registry_load(s_reg);
    bool require_confirm = settings_get_require_confirm();
    pairing_init(&s_fsm, require_confirm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "registry load failed: %s", esp_err_to_name(err));
    }

    const esp_timer_create_args_t pt = {.callback = pair_tick_cb, .name = "ble_pair"};
    const esp_timer_create_args_t rt = {.callback = reconnect_timer_cb, .name = "ble_reconn"};
    const esp_timer_create_args_t st = {.callback = stats_timer_cb, .name = "ble_stats"};
    if (esp_timer_create(&pt, &s_pair_timer) != ESP_OK || esp_timer_create(&rt, &s_reconnect_timer) != ESP_OK ||
        esp_timer_create(&st, &s_stats_timer) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    /* Security (CLAUDE.md §3.4): bonding on, LE Secure Connections when the peer supports them, MITM
     * protection requested, IO capability DisplayOnly: keyboards get a passkey shown in the web app. Devices
     * that cannot do passkey entry (mice) fall back to Just Works and go through the quarantine challenge. */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init(); /* Bonds persisted in NVS (CONFIG_BT_NIMBLE_NVS_PERSIST). */
    nimble_port_freertos_init(host_task);
    esp_timer_start_periodic(s_stats_timer, STATS_PERIOD_US);
    esp_timer_start_periodic(s_pair_timer, PAIR_TICK_US);
    return ESP_OK;
}

/** @copydoc ble_central_is_ready */
bool ble_central_is_ready(void) {
    return s_synced;
}

/* ---- Public API --------------------------------------------------------------------------------- */

/** @copydoc ble_central_get_device */
bool ble_central_get_device(unsigned slot, ble_device_info_t *out) {
    if (slot >= BLE_CENTRAL_MAX_DEVICES) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    LOCK();
    const dev_t *d = &s_dev[slot];
    out->slot = (uint8_t)slot;
    out->in_use = s_reg[slot].used && !d->pending;
    if (out->in_use) {
        addr_to_id(s_reg[slot].addr, out->id);
        strlcpy(out->name, s_reg[slot].name, sizeof(out->name));
        out->addr_type = s_reg[slot].addr_type;
        out->connected = d->conn != NO_CONN;
        out->ready = d->ready;
        out->keyboard = d->ready && hogp_is_keyboard(&d->hogp);
        out->boot_fallback = d->ready && d->hogp.boot_fallback;
        out->battery = d->battery;
        out->security = (sec_level_t)s_reg[slot].sec;
        out->rssi = d->rssi;
        out->conn_itvl_125us = d->itvl;
        out->conn_latency = d->latency;
        out->conn_timeout_10ms = d->timeout;
        out->reconnects = d->reconnects;
        out->last_seen_s = d->last_seen_s;
        lat_stats_summary(&d->lat, &out->latency);
    }
    UNLOCK();
    return true;
}

/** @copydoc ble_central_find_by_id */
int ble_central_find_by_id(const char *id) {
    for (unsigned i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        ble_device_info_t info;
        if (ble_central_get_device(i, &info) && info.in_use && strcmp(info.id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/** @copydoc ble_central_scan_start */
esp_err_t ble_central_scan_start(uint32_t duration_ms) {
    LOCK();
    if (!s_synced) {
        UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }
    if (registry_free_slot(s_reg) < 0) {
        UNLOCK();
        return ESP_ERR_NO_MEM; /* Four devices already registered. */
    }
    pair_start_t r = pairing_begin_scan(&s_fsm, now_ms());
    if (r != PAIR_START_OK) {
        UNLOCK();
        return r == PAIR_START_LOCKED ? ESP_ERR_NOT_ALLOWED : ESP_ERR_INVALID_STATE;
    }
    bool was_bg = s_bg_active;
    s_pair.nscan = 0;
    UNLOCK();
    if (was_bg) {
        ble_gap_conn_cancel(); /* A connect attempt and a scan cannot run together. */
    }
    struct ble_gap_disc_params dp = {.itvl = SCAN_ITVL, .window = SCAN_WINDOW, .filter_duplicates = 1};
    int rc = ble_gap_disc(s_own_addr_type, (int32_t)duration_ms, &dp, scan_event, NULL);
    /* A just-cancelled background connect may still hold the controller for a few ms: retry briefly. */
    for (int tries = 0; was_bg && (rc == BLE_HS_EBUSY || rc == BLE_HS_EALREADY) && tries < SCAN_RETRIES; tries++) {
        vTaskDelay(pdMS_TO_TICKS(SCAN_RETRY_MS));
        rc = ble_gap_disc(s_own_addr_type, (int32_t)duration_ms, &dp, scan_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "scan start failed (%d)", rc);
        LOCK();
        pairing_scan_done(&s_fsm);
        UNLOCK();
        reconcile();
        return ESP_FAIL;
    }
    changed(-1);
    return ESP_OK;
}

/** @copydoc ble_central_scan_results */
size_t ble_central_scan_results(ble_scan_result_t *out) {
    LOCK();
    size_t n = s_pair.nscan;
    memcpy(out, s_pair.scan, n * sizeof(out[0]));
    UNLOCK();
    return n;
}

/** @copydoc ble_central_pair */
esp_err_t ble_central_pair(const uint8_t addr[6], uint8_t addr_type, bool allow_legacy) {
    ble_addr_t a = {.type = addr_type};
    for (int i = 0; i < 6; i++) {
        a.val[i] = addr[5 - i]; /* Display order -> NimBLE order. */
    }
    LOCK();
    if (!s_synced) {
        UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }
    int slot = registry_find(s_reg, a.val);
    if (slot < 0) {
        slot = registry_free_slot(s_reg);
    }
    if (slot < 0) {
        UNLOCK();
        return ESP_ERR_NO_MEM;
    }
    if (s_dev[slot].conn != NO_CONN) {
        UNLOCK();
        return ESP_ERR_INVALID_STATE; /* Already connected and registered. */
    }
    /* Physical confirmation needs an ALREADY paired keyboard other than the candidate (CLAUDE.md §3.4.3). */
    bool other_keyboard = false;
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        const bool kbd = i != slot && s_reg[i].used && !s_dev[i].pending && s_reg[i].kbd;
        if (kbd) {
            ESP_LOGI(TAG, "slot %d (\"%s\") counts as an already paired keyboard: physical confirmation applies", i,
                     s_reg[i].name);
        }
        other_keyboard = other_keyboard || kbd;
    }
    bool scanning = s_fsm.state == PAIR_SCANNING;
    pair_start_t r = pairing_begin_pair(&s_fsm, now_ms(), allow_legacy, other_keyboard);
    if (r != PAIR_START_OK) {
        UNLOCK();
        return r == PAIR_START_LOCKED ? ESP_ERR_NOT_ALLOWED : ESP_ERR_INVALID_STATE;
    }
    /* Reserve the slot; it is persisted only once the pairing completes. A re-pair of a known device first
     * removes its old bond, so the new keys replace it. */
    char name[BLE_CENTRAL_NAME_MAX] = "";
    s_pair.appearance = 0;
    s_pair.rx_reports = 0;
    for (size_t i = 0; i < s_pair.nscan; i++) {
        if (memcmp(s_pair.scan[i].addr, addr, 6) == 0) {
            strlcpy(name, s_pair.scan[i].name, sizeof(name));
            s_pair.appearance = s_pair.scan[i].appearance;
        }
    }
    memset(&s_reg[slot], 0, sizeof(s_reg[slot]));
    s_reg[slot].used = 1;
    s_reg[slot].addr_type = addr_type;
    memcpy(s_reg[slot].addr, a.val, 6);
    strlcpy(s_reg[slot].name, name[0] ? name : "BLE device", sizeof(s_reg[slot].name));
    s_dev[slot].pending = true;
    s_pair.addr = a;
    s_pair.slot = slot;
    s_pair.connect_requested = true;
    UNLOCK();
    ble_store_util_delete_peer(&a);
    if (scanning) {
        ble_gap_disc_cancel();
    }
    changed(-1);
    reconcile();
    return ESP_OK;
}

/** @copydoc ble_central_pair_cancel */
void ble_central_pair_cancel(void) {
    LOCK();
    pair_state_t st = s_fsm.state;
    pairing_cancel(&s_fsm);
    UNLOCK();
    if (st == PAIR_SCANNING) {
        ble_gap_disc_cancel();
        changed(-1);
        reconcile();
    } else if (pairing_active(&s_fsm) || st == PAIR_CONNECTING || st == PAIR_PAIRING || st == PAIR_QUARANTINE ||
               st == PAIR_CONFIRM) {
        pair_sync();
    }
}

/** @copydoc ble_central_pair_state */
pair_state_t ble_central_pair_state(void) {
    return s_fsm.state;
}

/** @copydoc ble_central_pairing_status */
void ble_central_pairing_status(ble_pairing_status_t *out) {
    memset(out, 0, sizeof(*out));
    uint32_t t = now_ms();
    LOCK();
    out->state = s_fsm.state;
    out->fail = s_fsm.fail;
    out->level = s_fsm.level;
    out->has_passkey = s_fsm.has_passkey;
    out->passkey = s_fsm.passkey;
    out->challenge_kind = s_fsm.challenge.kind;
    memcpy(out->challenge, s_fsm.challenge.text, sizeof(out->challenge));
    out->challenge_pos = s_fsm.challenge.pos;
    out->challenge_len = s_fsm.challenge.len;
    out->remaining_ms = pairing_remaining_ms(&s_fsm, t);
    out->lock_remaining_s = pairing_lock_remaining_s(&s_fsm, t);
    out->rx_reports = s_pair.rx_reports;
    out->need_confirm = s_fsm.need_confirm;
    out->require_confirm = s_fsm.require_confirm;
    if (s_pair.slot >= 0 && s_reg[s_pair.slot].used) {
        strlcpy(out->name, s_reg[s_pair.slot].name, sizeof(out->name));
    }
    UNLOCK();
}

/** @copydoc ble_central_set_require_confirm */
void ble_central_set_require_confirm(bool enabled) {
    LOCK();
    s_fsm.require_confirm = enabled;
    UNLOCK();
}

/** @copydoc ble_central_physical_confirm */
void ble_central_physical_confirm(void) {
    LOCK();
    bool waiting = s_fsm.state == PAIR_CONFIRM;
    pairing_confirm(&s_fsm, now_ms());
    UNLOCK();
    if (waiting) {
        pair_sync();
    }
}

/** @copydoc ble_central_quarantined */
bool ble_central_quarantined(unsigned slot) {
    LOCK();
    bool q = s_fsm.state == PAIR_QUARANTINE && s_pair.slot == (int)slot;
    UNLOCK();
    return q;
}

/** @copydoc ble_central_challenge_buttons */
void ble_central_challenge_buttons(uint8_t buttons) {
    LOCK();
    uint8_t pos = s_fsm.challenge.pos;
    pair_state_t before = s_fsm.state;
    pairing_input_buttons(&s_fsm, now_ms(), buttons);
    bool moved = s_fsm.state != before || s_fsm.challenge.pos != pos;
    UNLOCK();
    if (moved) {
        pair_sync(); /* Also covers progress updates for the web app. */
    }
}

/** @copydoc ble_central_challenge_keys */
void ble_central_challenge_keys(const uint8_t keys[6]) {
    LOCK();
    uint8_t pos = s_fsm.challenge.pos;
    pair_state_t before = s_fsm.state;
    pairing_input_keys(&s_fsm, now_ms(), keys);
    bool moved = s_fsm.state != before || s_fsm.challenge.pos != pos;
    UNLOCK();
    if (moved) {
        pair_sync();
    }
}

/** @copydoc ble_central_forget */
esp_err_t ble_central_forget(unsigned slot) {
    if (slot >= BLE_CENTRAL_MAX_DEVICES) {
        return ESP_ERR_NOT_FOUND;
    }
    LOCK();
    if (!s_reg[slot].used || s_dev[slot].pending) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }
    ble_addr_t a = {.type = s_reg[slot].addr_type};
    memcpy(a.val, s_reg[slot].addr, 6);
    uint16_t conn = s_dev[slot].conn;
    memset(&s_reg[slot], 0, sizeof(s_reg[slot]));
    save_registry();
    bool bg = s_bg_active;
    UNLOCK();
    ble_store_util_delete_peer(&a); /* The bond goes first so a reconnect cannot succeed any more. */
    if (conn != NO_CONN) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (bg) {
        ble_gap_conn_cancel(); /* Rebuild the allow-list without the forgotten device. */
    }
    changed((int)slot);
    return ESP_OK;
}

/** @copydoc ble_central_rename */
esp_err_t ble_central_rename(unsigned slot, const char *name) {
    if (slot >= BLE_CENTRAL_MAX_DEVICES || name == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    LOCK();
    if (!s_reg[slot].used || s_dev[slot].pending) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }
    strlcpy(s_reg[slot].name, name, sizeof(s_reg[slot].name));
    save_registry();
    UNLOCK();
    changed((int)slot);
    return ESP_OK;
}

/** @copydoc ble_central_get_report_map */
size_t ble_central_get_report_map(unsigned slot, uint8_t *out, size_t cap) {
    if (slot >= BLE_CENTRAL_MAX_DEVICES) {
        return 0;
    }
    LOCK();
    size_t n = 0;
    if (s_dev[slot].ready && s_dev[slot].hogp.map_len > 0) {
        n = s_dev[slot].hogp.map_len < cap ? s_dev[slot].hogp.map_len : cap;
        memcpy(out, s_dev[slot].hogp.map, n);
    }
    UNLOCK();
    return n;
}

/** @copydoc ble_central_record_latency */
void ble_central_record_latency(unsigned slot, uint32_t us) {
    if (slot < BLE_CENTRAL_MAX_DEVICES) {
        LOCK();
        lat_stats_record(&s_dev[slot].lat, us);
        UNLOCK();
    }
}

/** @copydoc ble_central_write_leds */
void ble_central_write_leds(uint8_t leds) {
    s_leds = leds;
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        LOCK();
        bool ready = s_dev[i].ready;
        UNLOCK();
        if (ready) {
            hogp_write_leds(&s_dev[i].hogp, leds);
        }
    }
}

/** @copydoc ble_central_connected_count */
unsigned ble_central_connected_count(void) {
    unsigned n = 0;
    LOCK();
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        n += (s_dev[i].ready && !s_dev[i].pending) ? 1u : 0u;
    }
    UNLOCK();
    return n;
}

/** @copydoc ble_central_set_conn_profile */
void ble_central_set_conn_profile(uint16_t itvl_units, uint16_t latency, uint16_t timeout_10ms) {
    uint16_t handles[BLE_CENTRAL_MAX_DEVICES];
    size_t n = 0;
    LOCK();
    s_profile.itvl = itvl_units;
    s_profile.latency = latency;
    s_profile.timeout = timeout_10ms;
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        if (s_dev[i].conn != NO_CONN) {
            handles[n++] = s_dev[i].conn;
        }
    }
    UNLOCK();
    /* The controller call is made outside the lock; it only queues a request. Peripherals may refuse it, which is
     * harmless: the link just keeps the old parameters (the stats timer shows what was really negotiated). */
    for (size_t i = 0; i < n; i++) {
        struct ble_gap_upd_params up = {
            .itvl_min = itvl_units, .itvl_max = itvl_units, .latency = latency, .supervision_timeout = timeout_10ms};
        const int rc = ble_gap_update_params(handles[i], &up);
        if (rc != 0) {
            ESP_LOGW(TAG, "connection update rejected locally (%d)", rc);
        }
    }
    ESP_LOGI(TAG, "connection profile: %.2f ms, latency %u", itvl_units * 1.25, (unsigned)latency);
}

/** @copydoc ble_central_has_keyboard */
bool ble_central_has_keyboard(void) {
    bool yes = false;
    LOCK();
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        yes = yes || (s_reg[i].used && !s_dev[i].pending && s_reg[i].kbd);
    }
    UNLOCK();
    return yes;
}

/** @copydoc ble_central_bonded_count */
unsigned ble_central_bonded_count(void) {
    unsigned n = 0;
    LOCK();
    for (int i = 0; i < BLE_CENTRAL_MAX_DEVICES; i++) {
        n += (s_reg[i].used && !s_dev[i].pending) ? 1u : 0u;
    }
    UNLOCK();
    return n;
}
