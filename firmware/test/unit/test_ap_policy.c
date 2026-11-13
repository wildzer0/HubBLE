/**
 * @file test_ap_policy.c
 * @brief Host unit tests for the AP on/off policy.
 */
#include "ap_policy.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

static const ap_policy_config_t k_cfg = {.boot_window_s = 300, .idle_timeout_s = 180};

/** Build inputs with one bonded device and no activity. */
static ap_policy_inputs_t quiet(uint64_t now_s) {
    ap_policy_inputs_t in = {.now_ms = now_s * 1000, .bonded_devices = 1};
    return in;
}

/** No bonded devices: AP always on, no countdown. */
static void test_no_bonds_always_on(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = {.now_ms = 10 * 3600 * 1000ULL};
    ap_policy_decision_t d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.ap_on);
    TEST_ASSERT_EQUAL_INT32(-1, d.remaining_s);
}

/** Boot window: on with a countdown, then off. */
static void test_boot_window(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(100);
    ap_policy_decision_t d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.ap_on);
    TEST_ASSERT_EQUAL_INT32(200, d.remaining_s);
    in = quiet(300);
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
}

/** An open session keeps the AP on; after the client leaves the idle timeout applies. */
static void test_session_then_idle_timeout(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(400); /* boot window over, but a client is using the app */
    in.clients = 1;
    in.ws_alive = true;
    ap_policy_decision_t d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.ap_on);
    TEST_ASSERT_EQUAL_INT32(-1, d.remaining_s);
    in = quiet(500); /* client last seen at t=400, gone at t=500 */
    d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.ap_on);
    TEST_ASSERT_EQUAL_INT32(80, d.remaining_s); /* last activity at t=400, idle timeout 180 s */
    in = quiet(580);
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
}

/** A connected client without an open web app does not keep the AP alive. */
static void test_client_without_webapp(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(400);
    in.clients = 1;
    in.ws_alive = false;
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
}

/** OTA holds the AP on and the idle timer restarts afterwards. */
static void test_ota_holds_ap(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(1000);
    in.ota_active = true;
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    in = quiet(1010);
    ap_policy_decision_t d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.ap_on);
    TEST_ASSERT_EQUAL_INT32(170, d.remaining_s);
}

/** A force-on request (Shift combo) switches an expired AP back on. */
/** "Off now" switches the AP off at once, even inside the boot window and with a client connected, and sticks. */
static void test_force_off_sticks_until_force_on(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(10); /* inside the boot window */
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    in.force_off = true;
    in.clients = 1;
    in.ws_alive = true;
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
    in = quiet(11); /* the request was one-shot, the state sticks */
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
    in = quiet(12);
    in.force_on = true; /* the Ctrl + Alt gesture */
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    in = quiet(13);
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on); /* back to the normal rules */
}

/** "Off now" is ignored while nothing is bonded and while an OTA runs. */
static void test_force_off_ignored_without_bonds_or_during_ota(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = {.now_ms = 5000, .force_off = true}; /* no bonded device */
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    in = quiet(6);
    in.force_off = true;
    in.ota_active = true;
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    in = quiet(7);
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on); /* it did not stick */
}

/** The USB configuration window ignores "Wi-Fi off now" and is extended by a USB session. */
static void test_config_window_survives_force_off_and_follows_usb_activity(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(10);
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).config_open); /* boot window */
    in.force_off = true;
    ap_policy_decision_t d = ap_policy_step(&p, &in);
    TEST_ASSERT_FALSE(d.ap_on);
    TEST_ASSERT_TRUE(d.config_open);
    in = quiet(400); /* boot window over, nothing happening */
    d = ap_policy_step(&p, &in);
    TEST_ASSERT_FALSE(d.config_open);
    in = quiet(401);
    in.usb_alive = true; /* a session over USB reopens it and keeps it open ... */
    d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.config_open);
    TEST_ASSERT_FALSE(d.ap_on); /* ... but never switches the Wi-Fi on */
    in = quiet(500);
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).config_open); /* idle tail after the session */
}

static void test_force_on(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = quiet(900);
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
    in.force_on = true;
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    in = quiet(901);
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
}

/** Host suspended: AP off even without bonds, but an OTA keeps it; the boot window survives the sleep. */
static void test_host_suspend(void) {
    ap_policy_t p;
    ap_policy_init(&p, &k_cfg);
    ap_policy_inputs_t in = {.now_ms = 10 * 1000ULL, .bonded_devices = 0, .host_suspended = true};
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
    in.ota_active = true;
    TEST_ASSERT_TRUE(ap_policy_step(&p, &in).ap_on);
    /* Bonded, inside the boot window: suspended -> off, resumed -> on again with the remaining time. */
    in = quiet(100);
    in.host_suspended = true;
    TEST_ASSERT_FALSE(ap_policy_step(&p, &in).ap_on);
    in = quiet(150);
    ap_policy_decision_t d = ap_policy_step(&p, &in);
    TEST_ASSERT_TRUE(d.ap_on);
    TEST_ASSERT_EQUAL_INT32(150, d.remaining_s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_bonds_always_on);
    RUN_TEST(test_boot_window);
    RUN_TEST(test_session_then_idle_timeout);
    RUN_TEST(test_client_without_webapp);
    RUN_TEST(test_ota_holds_ap);
    RUN_TEST(test_force_off_sticks_until_force_on);
    RUN_TEST(test_force_off_ignored_without_bonds_or_during_ota);
    RUN_TEST(test_config_window_survives_force_off_and_follows_usb_activity);
    RUN_TEST(test_force_on);
    RUN_TEST(test_host_suspend);
    return UNITY_END();
}
