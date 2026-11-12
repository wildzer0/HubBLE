/**
 * @file led_pattern.h
 * @brief Pure LED status patterns (CLAUDE.md §3.6): inputs describing the dongle, time -> colour.
 *
 * Priority, highest first: host suspended or LED disabled (off) > OTA (purple fast blink) > quarantine/challenge
 * (magenta blink) > pairing (cyan fast blink) > AP on (blue breathing) > no device connected (slow amber pulse) >
 * device connected (off, or dim green when the "status LED" setting is on).
 *
 * Dependencies: libc only (host-testable). Threading: stateless.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the LED has to show. */
typedef struct {
    bool host_suspended; /**< USB bus suspended: LED off. */
    bool ota_active;     /**< An update is being installed. */
    bool quarantine;     /**< A device is in the quarantine challenge. */
    bool pairing;        /**< Scanning, connecting or pairing. */
    bool ap_on;          /**< The Wi-Fi access point is on. */
    uint32_t connected;  /**< Number of connected peripherals. */
    bool status_led;     /**< Show dim green while at least one device is connected. */
    uint8_t brightness;  /**< 0..100 percent; 0 disables the LED. */
} led_inputs_t;

/** An RGB colour, 0..255 per channel (already scaled by the brightness). */
typedef struct {
    uint8_t r; /**< Red. */
    uint8_t g; /**< Green. */
    uint8_t b; /**< Blue. */
} led_rgb_t;

/**
 * @brief Colour to show at a given time.
 *
 * @param in   What the dongle is doing.
 * @param t_ms Monotonic time in milliseconds (drives blinking and breathing).
 * @return The colour, (0,0,0) when the LED should be off.
 */
led_rgb_t led_pattern(const led_inputs_t *in, uint32_t t_ms);

#ifdef __cplusplus
}
#endif
