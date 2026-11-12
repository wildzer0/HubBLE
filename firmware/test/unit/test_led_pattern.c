/**
 * @file test_led_pattern.c
 * @brief Host unit tests for the LED status patterns.
 */
#include "led_pattern.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Inputs with one connected device and nothing special going on. */
static led_inputs_t calm(void) {
    led_inputs_t in = {.connected = 1, .brightness = 100};
    return in;
}

static bool is_off(led_rgb_t c) {
    return c.r == 0 && c.g == 0 && c.b == 0;
}

/** Suspended host or brightness 0 turns everything off, whatever else is going on. */
static void test_off_cases(void) {
    led_inputs_t in = calm();
    in.ota_active = true;
    in.host_suspended = true;
    TEST_ASSERT_TRUE(is_off(led_pattern(&in, 0)));
    in.host_suspended = false;
    in.brightness = 0;
    TEST_ASSERT_TRUE(is_off(led_pattern(&in, 0)));
}

/** Normal use with a device connected is dark, or dim green when the status LED is on. */
static void test_connected(void) {
    led_inputs_t in = calm();
    TEST_ASSERT_TRUE(is_off(led_pattern(&in, 1234)));
    in.status_led = true;
    led_rgb_t c = led_pattern(&in, 1234);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
    TEST_ASSERT_GREATER_THAN_UINT8(0, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.b);
    TEST_ASSERT_LESS_THAN_UINT8(100, c.g); /* dim */
}

/** No device connected: amber pulse that reaches its peak mid-period and is dark at the start. */
static void test_amber_pulse(void) {
    led_inputs_t in = calm();
    in.connected = 0;
    TEST_ASSERT_TRUE(is_off(led_pattern(&in, 0)));
    led_rgb_t peak = led_pattern(&in, 2000);
    TEST_ASSERT_EQUAL_UINT8(255, peak.r);
    TEST_ASSERT_GREATER_THAN_UINT8(0, peak.g);
    TEST_ASSERT_EQUAL_UINT8(0, peak.b);
    TEST_ASSERT_LESS_THAN_UINT8(peak.r, led_pattern(&in, 1000).r);
}

/** AP on: blue breathing, and it wins over the amber "no device" pulse. */
static void test_ap_breathing(void) {
    led_inputs_t in = calm();
    in.connected = 0;
    in.ap_on = true;
    led_rgb_t peak = led_pattern(&in, 1500);
    TEST_ASSERT_EQUAL_UINT8(255, peak.b);
    TEST_ASSERT_EQUAL_UINT8(0, peak.r);
    TEST_ASSERT_TRUE(led_pattern(&in, 300).b < peak.b);
}

/** Priority: OTA > quarantine > pairing > AP, and each has its colour and blinks. */
static void test_priorities_and_colours(void) {
    led_inputs_t in = calm();
    in.ap_on = true;
    in.pairing = true;
    led_rgb_t cyan = led_pattern(&in, 0); /* first half of the period: on */
    TEST_ASSERT_EQUAL_UINT8(0, cyan.r);
    TEST_ASSERT_EQUAL_UINT8(255, cyan.g);
    TEST_ASSERT_EQUAL_UINT8(255, cyan.b);
    TEST_ASSERT_TRUE(is_off(led_pattern(&in, 200))); /* second half: off */

    in.quarantine = true;
    led_rgb_t magenta = led_pattern(&in, 0);
    TEST_ASSERT_EQUAL_UINT8(255, magenta.r);
    TEST_ASSERT_EQUAL_UINT8(0, magenta.g);
    TEST_ASSERT_EQUAL_UINT8(255, magenta.b);

    in.ota_active = true;
    led_rgb_t purple = led_pattern(&in, 0);
    TEST_ASSERT_EQUAL_UINT8(160, purple.r);
    TEST_ASSERT_EQUAL_UINT8(0, purple.g);
    TEST_ASSERT_EQUAL_UINT8(255, purple.b);
}

/** Brightness scales every channel, and values above 100 are clamped. */
static void test_brightness(void) {
    led_inputs_t in = calm();
    in.pairing = true;
    in.brightness = 50;
    led_rgb_t half = led_pattern(&in, 0);
    TEST_ASSERT_EQUAL_UINT8(127, half.g);
    in.brightness = 250;
    TEST_ASSERT_EQUAL_UINT8(255, led_pattern(&in, 0).g);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_off_cases);
    RUN_TEST(test_connected);
    RUN_TEST(test_amber_pulse);
    RUN_TEST(test_ap_breathing);
    RUN_TEST(test_priorities_and_colours);
    RUN_TEST(test_brightness);
    return UNITY_END();
}
