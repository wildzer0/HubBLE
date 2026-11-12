/**
 * @file test_captive_probe.c
 * @brief Host tests for the captive-portal probe classifier.
 */
#include <string.h>

#include "captive_probe.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

void test_known_probes(void) {
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_ANDROID, captive_probe_classify("/generate_204"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_ANDROID, captive_probe_classify("/gen_204"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_APPLE, captive_probe_classify("/hotspot-detect.html"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_MS_CONNECT, captive_probe_classify("/connecttest.txt"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_MS_NCSI, captive_probe_classify("/ncsi.txt"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_FIREFOX, captive_probe_classify("/success.txt"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_NETWORKMGR, captive_probe_classify("/check_network_status.txt"));
}

void test_other_requests_are_not_probes(void) {
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_NONE, captive_probe_classify("/"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_NONE, captive_probe_classify("/index.html"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_NONE, captive_probe_classify("/generate_2040"));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_NONE, captive_probe_classify(""));
    TEST_ASSERT_EQUAL(CAPTIVE_PROBE_NONE, captive_probe_classify(NULL));
}

void test_expected_answers(void) {
    TEST_ASSERT_EQUAL(204, captive_probe_status(CAPTIVE_PROBE_ANDROID));
    TEST_ASSERT_EQUAL_STRING("", captive_probe_body(CAPTIVE_PROBE_ANDROID));
    TEST_ASSERT_EQUAL(200, captive_probe_status(CAPTIVE_PROBE_APPLE));
    TEST_ASSERT_NOT_NULL(strstr(captive_probe_body(CAPTIVE_PROBE_APPLE), "Success"));
    TEST_ASSERT_EQUAL_STRING("Microsoft Connect Test", captive_probe_body(CAPTIVE_PROBE_MS_CONNECT));
    TEST_ASSERT_EQUAL_STRING("Microsoft NCSI", captive_probe_body(CAPTIVE_PROBE_MS_NCSI));
    TEST_ASSERT_EQUAL_STRING("success\n", captive_probe_body(CAPTIVE_PROBE_FIREFOX));
    TEST_ASSERT_EQUAL_STRING("text/html", captive_probe_type(CAPTIVE_PROBE_APPLE));
    TEST_ASSERT_EQUAL_STRING("text/plain", captive_probe_type(CAPTIVE_PROBE_FIREFOX));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_known_probes);
    RUN_TEST(test_other_requests_are_not_probes);
    RUN_TEST(test_expected_answers);
    return UNITY_END();
}
