/**
 * @file led_pattern.c
 * @brief Pure LED status patterns. See led_pattern.h.
 *
 * Dependencies: libc. Threading: stateless.
 */
#include "led_pattern.h"

/** Full-scale channel value before the brightness is applied. */
#define FULL 255
/** Blink periods in ms: OTA (fast), pairing (fast), quarantine (slower so the challenge stays readable). */
#define PERIOD_OTA_MS        400
#define PERIOD_PAIRING_MS    300
#define PERIOD_QUARANTINE_MS 800
/** Breathing and pulse periods in ms. */
#define PERIOD_BREATH_MS 3000
#define PERIOD_AMBER_MS  4000
/** Dim level of the optional "device connected" indicator. */
#define STATUS_GREEN 40

/**
 * @brief Triangle wave 0..255..0 over @p period.
 *
 * @param t_ms   Time.
 * @param period Period in ms.
 * @return Intensity 0..255.
 */
static uint32_t triangle(uint32_t t_ms, uint32_t period) {
    const uint32_t ph = t_ms % period;
    const uint32_t half = period / 2;
    const uint32_t up = ph < half ? ph : period - ph;
    return up * FULL / half;
}

/**
 * @brief Whether a 50 % duty blink is in its "on" half.
 *
 * @param t_ms   Time.
 * @param period Period in ms.
 * @return true during the first half of each period.
 */
static bool blink_on(uint32_t t_ms, uint32_t period) {
    return (t_ms % period) < period / 2;
}

/**
 * @brief Scale a colour by an intensity (0..255) and the brightness (0..100).
 *
 * @param r         Red 0..255.
 * @param g         Green 0..255.
 * @param b         Blue 0..255.
 * @param intensity 0..255 (for breathing); 255 = as is.
 * @param bright    0..100.
 * @return The scaled colour.
 */
static led_rgb_t scale(uint32_t r, uint32_t g, uint32_t b, uint32_t intensity, uint32_t bright) {
    const uint32_t k = intensity * bright; /* 0..25500 */
    led_rgb_t c = {(uint8_t)(r * k / (FULL * 100)), (uint8_t)(g * k / (FULL * 100)), (uint8_t)(b * k / (FULL * 100))};
    return c;
}

/** @copydoc led_pattern */
led_rgb_t led_pattern(const led_inputs_t *in, uint32_t t_ms) {
    const led_rgb_t off = {0, 0, 0};
    const uint32_t bright = in->brightness > 100 ? 100 : in->brightness;
    if (in->host_suspended || bright == 0) {
        return off;
    }
    if (in->ota_active) {
        return blink_on(t_ms, PERIOD_OTA_MS) ? scale(160, 0, FULL, FULL, bright) : off; /* purple */
    }
    if (in->quarantine) {
        return blink_on(t_ms, PERIOD_QUARANTINE_MS) ? scale(FULL, 0, FULL, FULL, bright) : off; /* magenta */
    }
    if (in->pairing) {
        return blink_on(t_ms, PERIOD_PAIRING_MS) ? scale(0, FULL, FULL, FULL, bright) : off; /* cyan */
    }
    if (in->ap_on) {
        return scale(0, 0, FULL, triangle(t_ms, PERIOD_BREATH_MS), bright); /* blue breathing */
    }
    if (in->connected == 0) {
        return scale(FULL, 140, 0, triangle(t_ms, PERIOD_AMBER_MS), bright); /* slow amber pulse */
    }
    return in->status_led ? scale(0, STATUS_GREEN, 0, FULL, bright) : off;
}
