/**
 * @file test_pairing.c
 * @brief Host tests for the pairing state machine and challenges.
 */
#include <string.h>

#include "pairing.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

static uint32_t s_seq;
/** Deterministic RNG: returns 0,1,2,... so challenges are predictable. */
static uint32_t rng_seq(void) {
    return s_seq++;
}
static uint32_t rng_const7(void) {
    return 7;
}

/** Drive a machine to PAIRING. */
static void to_pairing(pairing_t *p, uint32_t t, bool legacy, bool other_kbd) {
    TEST_ASSERT_EQUAL(PAIR_START_OK, pairing_begin_pair(p, t, legacy, other_kbd));
    TEST_ASSERT_EQUAL(PAIR_CONNECTING, p->state);
    pairing_on_connected(p, t);
    TEST_ASSERT_EQUAL(PAIR_PAIRING, p->state);
}

void test_green_path_no_confirmation(void) {
    pairing_t p;
    pairing_init(&p, true);
    to_pairing(&p, 0, false, false); /* no other keyboard: confirmation skipped */
    s_seq = 123456;
    TEST_ASSERT_EQUAL_UINT32(123456, pairing_make_passkey(&p, rng_seq));
    TEST_ASSERT_TRUE(p.has_passkey);
    TEST_ASSERT_TRUE(pairing_on_secured(&p, 10, true, true));
    TEST_ASSERT_EQUAL(SEC_GREEN, p.level);
    TEST_ASSERT_FALSE(p.has_passkey);
    pairing_on_hid_ready(&p, 20, true, rng_seq);
    TEST_ASSERT_EQUAL(PAIR_BONDED, p.state);
}

void test_passkey_is_six_digits(void) {
    pairing_t p;
    pairing_init(&p, true);
    to_pairing(&p, 0, false, false);
    s_seq = 0xFFFFFFFFu;
    TEST_ASSERT_TRUE(pairing_make_passkey(&p, rng_seq) < 1000000u);
}

void test_green_path_with_confirmation(void) {
    pairing_t p;
    pairing_init(&p, true);
    to_pairing(&p, 0, false, true);
    pairing_on_secured(&p, 1, true, true);
    pairing_on_hid_ready(&p, 2, true, rng_seq);
    TEST_ASSERT_EQUAL(PAIR_CONFIRM, p.state);
    pairing_confirm(&p, 3);
    TEST_ASSERT_EQUAL(PAIR_BONDED, p.state);
}

void test_confirmation_setting_off(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, true);
    pairing_on_secured(&p, 1, true, true);
    pairing_on_hid_ready(&p, 2, true, rng_seq);
    TEST_ASSERT_EQUAL(PAIR_BONDED, p.state);
}

void test_confirm_times_out(void) {
    pairing_t p;
    pairing_init(&p, true);
    to_pairing(&p, 0, false, true);
    pairing_on_secured(&p, 1, true, true);
    pairing_on_hid_ready(&p, 2, true, rng_seq);
    pairing_tick(&p, 2 + PAIR_CONFIRM_TIMEOUT_MS - 1);
    TEST_ASSERT_EQUAL(PAIR_CONFIRM, p.state);
    pairing_tick(&p, 2 + PAIR_CONFIRM_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(PAIR_FAILED, p.state);
    TEST_ASSERT_EQUAL(FAIL_TIMEOUT, p.fail);
}

void test_mouse_quarantine_challenge(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    TEST_ASSERT_TRUE(pairing_on_secured(&p, 1, false, true));
    TEST_ASSERT_EQUAL(SEC_YELLOW, p.level);
    s_seq = 0; /* L R M L R M */
    pairing_on_hid_ready(&p, 2, false, rng_seq);
    TEST_ASSERT_EQUAL(PAIR_QUARANTINE, p.state);
    TEST_ASSERT_EQUAL(CHALLENGE_MOUSE, p.challenge.kind);
    TEST_ASSERT_EQUAL_STRING("LRMLRM", p.challenge.text);
    const uint8_t seq[6] = {0x01, 0x02, 0x04, 0x01, 0x02, 0x04};
    for (int i = 0; i < 6; i++) {
        pairing_input_buttons(&p, 10 + i * 2, seq[i]); /* press */
        TEST_ASSERT_EQUAL(i == 5 ? PAIR_BONDED : PAIR_QUARANTINE, p.state);
        pairing_input_buttons(&p, 11 + i * 2, 0); /* release */
    }
    TEST_ASSERT_EQUAL(SEC_YELLOW, p.level);
}

void test_held_button_counts_once(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, false, true);
    s_seq = 0;
    pairing_on_hid_ready(&p, 2, false, rng_seq);
    pairing_input_buttons(&p, 3, 0x01);
    pairing_input_buttons(&p, 4, 0x01); /* still held: no new edge */
    pairing_input_buttons(&p, 5, 0x01);
    TEST_ASSERT_EQUAL_UINT8(1, p.challenge.pos);
}

void test_mouse_challenge_wrong_clicks_fail(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, false, true);
    s_seq = 0;
    pairing_on_hid_ready(&p, 2, false, rng_seq); /* expects L first */
    for (int i = 0; i < CHALLENGE_MAX_MISTAKES; i++) {
        TEST_ASSERT_EQUAL(PAIR_QUARANTINE, p.state);
        pairing_input_buttons(&p, 3, 0x02); /* R: wrong */
        pairing_input_buttons(&p, 3, 0);
    }
    TEST_ASSERT_EQUAL(PAIR_FAILED, p.state);
    TEST_ASSERT_EQUAL(FAIL_CHALLENGE, p.fail);
}

void test_mouse_sequence_never_all_same(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, false, true);
    pairing_on_hid_ready(&p, 2, false, rng_const7); /* 7 % 3 = 1 -> would be RRRRRR */
    const char *t = p.challenge.text;
    TEST_ASSERT_FALSE(t[0] == t[5] && t[1] == t[5] && t[2] == t[5] && t[3] == t[5] && t[4] == t[5]);
}

void test_keyboard_challenge(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, false, true);
    s_seq = 0; /* alphabet[0..5] = ABCDEF */
    pairing_on_hid_ready(&p, 2, true, rng_seq);
    TEST_ASSERT_EQUAL(CHALLENGE_KEYBOARD, p.challenge.kind);
    TEST_ASSERT_EQUAL_STRING("ABCDEF", p.challenge.text);
    for (uint8_t u = 0x04; u <= 0x09; u++) { /* a..f */
        uint8_t keys[6] = {u, 0, 0, 0, 0, 0};
        pairing_input_keys(&p, 5, keys);
        uint8_t none[6] = {0};
        pairing_input_keys(&p, 6, none);
    }
    TEST_ASSERT_EQUAL(PAIR_BONDED, p.state);
}

void test_keyboard_challenge_rollover_keys_use_edges(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, false, true);
    s_seq = 0;
    pairing_on_hid_ready(&p, 2, true, rng_seq); /* ABCDEF */
    uint8_t k1[6] = {0x04, 0, 0, 0, 0, 0};
    uint8_t k2[6] = {0x04, 0x05, 0, 0, 0, 0}; /* B pressed while A still held */
    pairing_input_keys(&p, 3, k1);
    pairing_input_keys(&p, 4, k2);
    pairing_input_keys(&p, 5, k2); /* repeat of the same report: nothing new */
    TEST_ASSERT_EQUAL_UINT8(2, p.challenge.pos);
}

void test_challenge_timeout(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, false, true);
    pairing_on_hid_ready(&p, 100, false, rng_seq);
    TEST_ASSERT_EQUAL_UINT32(PAIR_CHALLENGE_TIMEOUT_MS, pairing_remaining_ms(&p, 100));
    pairing_tick(&p, 100 + PAIR_CHALLENGE_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(PAIR_FAILED, p.state);
    TEST_ASSERT_EQUAL(FAIL_TIMEOUT, p.fail);
}

void test_legacy_rejected_by_default(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, false, false);
    TEST_ASSERT_FALSE(pairing_on_secured(&p, 1, false, false));
    TEST_ASSERT_EQUAL(PAIR_FAILED, p.state);
    TEST_ASSERT_EQUAL(FAIL_LEGACY, p.fail);
}

void test_legacy_override_is_red(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 0, true, false);
    TEST_ASSERT_TRUE(pairing_on_secured(&p, 1, true, false)); /* even authenticated legacy stays red */
    TEST_ASSERT_EQUAL(SEC_RED, p.level);
}

void test_pairing_window_timeout(void) {
    pairing_t p;
    pairing_init(&p, false);
    to_pairing(&p, 1000, false, false);
    pairing_tick(&p, 1000 + PAIR_PAIRING_TIMEOUT_MS - 1);
    TEST_ASSERT_EQUAL(PAIR_PAIRING, p.state);
    pairing_tick(&p, 1000 + PAIR_PAIRING_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(FAIL_TIMEOUT, p.fail);
}

void test_lock_after_three_failures(void) {
    pairing_t p;
    pairing_init(&p, false);
    uint32_t t = 1000;
    for (int i = 0; i < PAIR_MAX_FAILURES; i++) {
        TEST_ASSERT_EQUAL(PAIR_START_OK, pairing_begin_pair(&p, t, false, false));
        pairing_fail(&p, t, FAIL_SECURITY);
        t += 10;
    }
    TEST_ASSERT_TRUE(p.locked);
    TEST_ASSERT_EQUAL(PAIR_START_LOCKED, pairing_begin_pair(&p, t, false, false));
    TEST_ASSERT_EQUAL(PAIR_START_LOCKED, pairing_begin_scan(&p, t));
    TEST_ASSERT_TRUE(pairing_lock_remaining_s(&p, t) > 0);
    TEST_ASSERT_EQUAL(PAIR_START_OK, pairing_begin_pair(&p, t + PAIR_LOCK_MS + 10, false, false));
}

void test_cancel_does_not_count_and_success_resets(void) {
    pairing_t p;
    pairing_init(&p, false);
    for (int i = 0; i < 5; i++) {
        pairing_begin_pair(&p, 0, false, false);
        pairing_cancel(&p);
        TEST_ASSERT_EQUAL(FAIL_CANCELLED, p.fail);
    }
    TEST_ASSERT_FALSE(p.locked);
    pairing_begin_pair(&p, 0, false, false);
    pairing_fail(&p, 0, FAIL_SECURITY);
    pairing_begin_pair(&p, 0, false, false);
    pairing_fail(&p, 0, FAIL_SECURITY);
    to_pairing(&p, 0, false, false);
    pairing_on_secured(&p, 1, true, true);
    pairing_on_hid_ready(&p, 2, true, rng_seq);
    TEST_ASSERT_EQUAL(PAIR_BONDED, p.state);
    TEST_ASSERT_EQUAL_UINT8(0, p.failures);
}

void test_busy_and_scan_to_pair(void) {
    pairing_t p;
    pairing_init(&p, false);
    TEST_ASSERT_EQUAL(PAIR_START_OK, pairing_begin_scan(&p, 0));
    TEST_ASSERT_EQUAL(PAIR_START_BUSY, pairing_begin_scan(&p, 0));
    TEST_ASSERT_EQUAL(PAIR_START_OK, pairing_begin_pair(&p, 0, false, false)); /* scan -> pair */
    TEST_ASSERT_EQUAL(PAIR_START_BUSY, pairing_begin_pair(&p, 0, false, false));
    TEST_ASSERT_EQUAL(PAIR_CONNECTING, p.state);
}

void test_scan_done_returns_to_idle(void) {
    pairing_t p;
    pairing_init(&p, false);
    pairing_begin_scan(&p, 0);
    pairing_scan_done(&p);
    TEST_ASSERT_EQUAL(PAIR_IDLE, p.state);
    pairing_begin_scan(&p, 0);
    pairing_cancel(&p);
    TEST_ASSERT_EQUAL(PAIR_IDLE, p.state);
}

void test_events_in_wrong_state_are_ignored(void) {
    pairing_t p;
    pairing_init(&p, false);
    pairing_confirm(&p, 0);
    pairing_input_buttons(&p, 0, 1);
    uint8_t k[6] = {4};
    pairing_input_keys(&p, 0, k);
    TEST_ASSERT_EQUAL_UINT32(0, pairing_make_passkey(&p, rng_seq));
    TEST_ASSERT_FALSE(pairing_on_secured(&p, 0, true, true));
    pairing_on_hid_ready(&p, 0, true, rng_seq);
    pairing_fail(&p, 0, FAIL_SECURITY);
    TEST_ASSERT_EQUAL(PAIR_IDLE, p.state);
}

void test_usage_to_char(void) {
    TEST_ASSERT_EQUAL_CHAR('A', challenge_usage_to_char(0x04));
    TEST_ASSERT_EQUAL_CHAR('Z', challenge_usage_to_char(0x1D));
    TEST_ASSERT_EQUAL_CHAR('1', challenge_usage_to_char(0x1E));
    TEST_ASSERT_EQUAL_CHAR('9', challenge_usage_to_char(0x26));
    TEST_ASSERT_EQUAL_CHAR('0', challenge_usage_to_char(0x27));
    TEST_ASSERT_EQUAL_CHAR(0, challenge_usage_to_char(0x28));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_green_path_no_confirmation);
    RUN_TEST(test_passkey_is_six_digits);
    RUN_TEST(test_green_path_with_confirmation);
    RUN_TEST(test_confirmation_setting_off);
    RUN_TEST(test_confirm_times_out);
    RUN_TEST(test_mouse_quarantine_challenge);
    RUN_TEST(test_held_button_counts_once);
    RUN_TEST(test_mouse_challenge_wrong_clicks_fail);
    RUN_TEST(test_mouse_sequence_never_all_same);
    RUN_TEST(test_keyboard_challenge);
    RUN_TEST(test_keyboard_challenge_rollover_keys_use_edges);
    RUN_TEST(test_challenge_timeout);
    RUN_TEST(test_legacy_rejected_by_default);
    RUN_TEST(test_legacy_override_is_red);
    RUN_TEST(test_pairing_window_timeout);
    RUN_TEST(test_lock_after_three_failures);
    RUN_TEST(test_cancel_does_not_count_and_success_resets);
    RUN_TEST(test_busy_and_scan_to_pair);
    RUN_TEST(test_scan_done_returns_to_idle);
    RUN_TEST(test_events_in_wrong_state_are_ignored);
    RUN_TEST(test_usage_to_char);
    return UNITY_END();
}
