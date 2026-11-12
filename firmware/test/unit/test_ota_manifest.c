/**
 * @file test_ota_manifest.c
 * @brief Host unit tests for the ota_manifest component.
 */
#include <string.h>

#include "ota_manifest.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** A valid SHA-256 for the fixtures. */
#define SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

/** A well-formed manifest with an entry for s3_zero only. */
static const char *GOOD = "{\"version\":\"0.7.0\",\"min_version\":\"0.5.0\",\"signed\":true,"
                          "\"release_notes_url\":\"https://example.org/notes\","
                          "\"boards\":{\"s3_zero\":{\"ota\":{\"file\":\"hubble-s3_zero-v0.7.0-ota.bin\","
                          "\"size\":1100000,\"sha256\":\"" SHA "\"}}}}";

/** The tag is found in a realistic, truncated API answer, skipping the author object. */
static void test_find_tag(void) {
    const char *api = "[{\"url\":\"https://api.github.com/x\",\"author\":{\"login\":\"a\",\"id\":1},"
                      "\"tag_name\":\"v0.7.0-rc.1\",\"name\":\"v0.7.0-rc.1\",\"asse";
    char tag[OTA_TAG_MAX];
    TEST_ASSERT_TRUE(ota_manifest_find_tag(api, strlen(api), tag, sizeof(tag)));
    TEST_ASSERT_EQUAL_STRING("v0.7.0-rc.1", tag);
}

/** Missing, truncated, oversized or odd tags are refused. */
static void test_find_tag_rejects(void) {
    char tag[8];
    const char *none = "{\"id\":1}";
    TEST_ASSERT_FALSE(ota_manifest_find_tag(none, strlen(none), tag, sizeof(tag)));
    const char *trunc = "{\"tag_name\":\"v0.7";
    TEST_ASSERT_FALSE(ota_manifest_find_tag(trunc, strlen(trunc), tag, sizeof(tag)));
    const char *longtag = "{\"tag_name\":\"v0.7.0-rc.1234\"}";
    TEST_ASSERT_FALSE(ota_manifest_find_tag(longtag, strlen(longtag), tag, sizeof(tag)));
    const char *bad = "{\"tag_name\":\"../../x\"}";
    char big[32];
    TEST_ASSERT_FALSE(ota_manifest_find_tag(bad, strlen(bad), big, sizeof(big)));
    TEST_ASSERT_FALSE(ota_manifest_find_tag(NULL, 0, big, sizeof(big)));
}

/** A good manifest yields the entry of the requested board. */
static void test_parse_ok(void) {
    ota_release_t r;
    TEST_ASSERT_EQUAL(OTA_MANIFEST_OK, ota_manifest_parse(GOOD, "s3_zero", &r));
    TEST_ASSERT_EQUAL_STRING("0.7.0", r.version);
    TEST_ASSERT_EQUAL_STRING("0.5.0", r.min_version);
    TEST_ASSERT_TRUE(r.signed_image);
    TEST_ASSERT_EQUAL_STRING("hubble-s3_zero-v0.7.0-ota.bin", r.file);
    TEST_ASSERT_EQUAL_UINT32(1100000, r.size);
    TEST_ASSERT_EQUAL_STRING(SHA, r.sha256);
}

/** Other board, bad JSON and malformed assets are told apart. */
static void test_parse_errors(void) {
    ota_release_t r;
    TEST_ASSERT_EQUAL(OTA_MANIFEST_NO_BOARD, ota_manifest_parse(GOOD, "xiao_s3", &r));
    TEST_ASSERT_EQUAL(OTA_MANIFEST_BAD_JSON, ota_manifest_parse("not json", "s3_zero", &r));
    TEST_ASSERT_EQUAL(OTA_MANIFEST_BAD_JSON, ota_manifest_parse("{\"version\":\"x\"}", "s3_zero", &r));

    char buf[600];
    /* Path traversal in the file name. */
    const char *evil = "{\"version\":\"0.7.0\",\"min_version\":\"0.1.0\",\"boards\":{\"s3_zero\":{\"ota\":"
                       "{\"file\":\"../x.bin\",\"size\":2000,\"sha256\":\"" SHA "\"}}}}";
    TEST_ASSERT_EQUAL(OTA_MANIFEST_BAD_ASSET, ota_manifest_parse(evil, "s3_zero", &r));
    /* Upper-case or short SHA, absurd size. */
    snprintf(buf, sizeof(buf),
             "{\"version\":\"0.7.0\",\"min_version\":\"0.1.0\",\"boards\":{\"s3_zero\":{\"ota\":"
             "{\"file\":\"a.bin\",\"size\":2000,\"sha256\":\"ABC\"}}}}");
    TEST_ASSERT_EQUAL(OTA_MANIFEST_BAD_ASSET, ota_manifest_parse(buf, "s3_zero", &r));
    const char *huge = "{\"version\":\"0.7.0\",\"min_version\":\"0.1.0\",\"boards\":{\"s3_zero\":{\"ota\":"
                       "{\"file\":\"a.bin\",\"size\":999999999,\"sha256\":\"" SHA "\"}}}}";
    TEST_ASSERT_EQUAL(OTA_MANIFEST_BAD_ASSET, ota_manifest_parse(huge, "s3_zero", &r));
}

/** The decision follows version order, minimum version and the signed requirement. */
static void test_decide(void) {
    ota_release_t r;
    TEST_ASSERT_EQUAL(OTA_MANIFEST_OK, ota_manifest_parse(GOOD, "s3_zero", &r));
    TEST_ASSERT_EQUAL(OTA_DECISION_INSTALL, ota_manifest_decide("0.6.0", &r, true));
    TEST_ASSERT_EQUAL(OTA_DECISION_UP_TO_DATE, ota_manifest_decide("0.7.0", &r, true));
    TEST_ASSERT_EQUAL(OTA_DECISION_DOWNGRADE, ota_manifest_decide("0.8.0", &r, true));
    TEST_ASSERT_EQUAL(OTA_DECISION_TOO_OLD, ota_manifest_decide("0.4.0", &r, true));
    TEST_ASSERT_EQUAL(OTA_DECISION_BAD_VERSION, ota_manifest_decide("dev-abc", &r, true));
    /* A pre-release of the same version is older than the release. */
    TEST_ASSERT_EQUAL(OTA_DECISION_INSTALL, ota_manifest_decide("0.7.0-rc.1", &r, true));
    r.signed_image = false;
    TEST_ASSERT_EQUAL(OTA_DECISION_UNSIGNED, ota_manifest_decide("0.6.0", &r, true));
    TEST_ASSERT_EQUAL(OTA_DECISION_INSTALL, ota_manifest_decide("0.6.0", &r, false));
}

/** SHA-256 hex validation. */
static void test_sha_hex(void) {
    TEST_ASSERT_TRUE(ota_sha256_hex_valid(SHA));
    TEST_ASSERT_FALSE(ota_sha256_hex_valid("0123"));
    TEST_ASSERT_FALSE(ota_sha256_hex_valid(NULL));
    char up[65];
    strcpy(up, SHA);
    up[3] = 'D';
    TEST_ASSERT_FALSE(ota_sha256_hex_valid(up));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_find_tag);
    RUN_TEST(test_find_tag_rejects);
    RUN_TEST(test_parse_ok);
    RUN_TEST(test_parse_errors);
    RUN_TEST(test_decide);
    RUN_TEST(test_sha_hex);
    return UNITY_END();
}
