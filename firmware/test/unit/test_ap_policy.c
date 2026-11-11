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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_bonds_always_on);
    RUN_TEST(test_boot_window);
    RUN_TEST(test_session_then_idle_timeout);
    RUN_TEST(test_client_without_webapp);
    RUN_TEST(test_ota_holds_ap);
    RUN_TEST(test_force_on);
    return UNITY_END();
}
