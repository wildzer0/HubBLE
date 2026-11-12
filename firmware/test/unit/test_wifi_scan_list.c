/**
 * @file test_wifi_scan_list.c
 * @brief Host unit tests for the Wi-Fi scan list clean-up.
 */
#include <string.h>

#include "unity.h"
#include "wifi_scan_list.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Fill one entry. */
static wifi_scan_net_t net(const char *ssid, int8_t rssi, bool secure) {
    wifi_scan_net_t n = {.rssi = rssi, .secure = secure};
    strncpy(n.ssid, ssid, sizeof(n.ssid) - 1);
    return n;
}

/** Hidden networks (empty SSID) are removed. */
static void test_hidden_removed(void) {
    wifi_scan_net_t l[] = {net("", -40, true), net("Home", -60, true)};
    TEST_ASSERT_EQUAL(1, wifi_scan_list_clean(l, 2, 20));
    TEST_ASSERT_EQUAL_STRING("Home", l[0].ssid);
}

/** Same SSID from several access points: the strongest one wins, with its own security flag. */
static void test_dedupe_keeps_strongest(void) {
    wifi_scan_net_t l[] = {net("Mesh", -80, true), net("Mesh", -45, false), net("Mesh", -60, true)};
    TEST_ASSERT_EQUAL(1, wifi_scan_list_clean(l, 3, 20));
    TEST_ASSERT_EQUAL_INT8(-45, l[0].rssi);
    TEST_ASSERT_FALSE(l[0].secure);
}

/** Strongest first; equal signals are ordered by name. */
static void test_sorted_by_signal_then_name(void) {
    wifi_scan_net_t l[] = {net("b", -70, true), net("z", -50, true), net("a", -70, false), net("m", -30, true)};
    TEST_ASSERT_EQUAL(4, wifi_scan_list_clean(l, 4, 20));
    TEST_ASSERT_EQUAL_STRING("m", l[0].ssid);
    TEST_ASSERT_EQUAL_STRING("z", l[1].ssid);
    TEST_ASSERT_EQUAL_STRING("a", l[2].ssid);
    TEST_ASSERT_EQUAL_STRING("b", l[3].ssid);
}

/** The list is truncated to the maximum, keeping the strongest. */
static void test_limit(void) {
    wifi_scan_net_t l[30];
    for (int i = 0; i < 30; i++) {
        char name[8];
        name[0] = 'n';
        name[1] = (char)('A' + i);
        name[2] = '\0';
        l[i] = net(name, (int8_t)(-90 + i), true);
    }
    TEST_ASSERT_EQUAL(20, wifi_scan_list_clean(l, 30, 20));
    TEST_ASSERT_EQUAL_INT8(-61, l[0].rssi);
    TEST_ASSERT_EQUAL_INT8(-80, l[19].rssi);
}

/** Empty input and all-hidden input give an empty list; names that differ only in case are different networks. */
static void test_edges(void) {
    wifi_scan_net_t l[] = {net("", -1, false), net("Abc", -50, true), net("abc", -51, true)};
    TEST_ASSERT_EQUAL(0, wifi_scan_list_clean(l, 0, 20));
    TEST_ASSERT_EQUAL(2, wifi_scan_list_clean(l, 3, 20));
    wifi_scan_net_t h[] = {net("", -1, false)};
    TEST_ASSERT_EQUAL(0, wifi_scan_list_clean(h, 1, 20));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hidden_removed);
    RUN_TEST(test_dedupe_keeps_strongest);
    RUN_TEST(test_sorted_by_signal_then_name);
    RUN_TEST(test_limit);
    RUN_TEST(test_edges);
    return UNITY_END();
}
