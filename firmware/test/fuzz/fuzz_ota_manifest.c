/**
 * @file fuzz_ota_manifest.c
 * @brief Fuzz target: release manifest and GitHub API answer parsing (network input, downloaded over HTTPS).
 */
#include <stdlib.h>
#include <string.h>

#include "ota_manifest.h"

/** libFuzzer entry point. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char tag[OTA_TAG_MAX];
    ota_manifest_find_tag((const char *)data, size, tag, sizeof(tag));

    char *text = malloc(size + 1); /* The parser needs a NUL terminated string. */
    if (text == NULL) {
        return 0;
    }
    memcpy(text, data, size);
    text[size] = '\0';
    ota_release_t rel;
    if (ota_manifest_parse(text, "s3_zero", &rel) == OTA_MANIFEST_OK) {
        ota_manifest_decide("0.6.0", &rel, true);
        ota_manifest_decide("not-semver", &rel, false);
    }
    free(text);
    return 0;
}
