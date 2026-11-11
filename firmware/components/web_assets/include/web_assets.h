/**
 * @file web_assets.h
 * @brief Access to the web app embedded in the firmware image.
 *
 * Responsibilities: expose the embedded bundle blob. M0 only proves the build
 * pipeline; the HTTP server that serves it arrives in M1.
 * Dependencies: web_bundle. Threading: read-only data, thread-safe.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the embedded bundle.
 *
 * @param[out] len Receives the bundle size in bytes.
 * @return Pointer to the (flash-mapped, read-only) bundle.
 */
const uint8_t *web_assets_get(size_t *len);

#ifdef __cplusplus
}
#endif
