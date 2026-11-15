/**
 * @file test_registry_codec.c
 * @brief Host tests for the registry blob decoder (older / newer slot counts, corruption).
 */
#include <string.h>

#include "registry_codec.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/** Build @p n records, record i named "dev<i>" with address byte i + 1. */
static void make(reg_rec_t *r, size_t n) {
    memset(r, 0, sizeof(*r) * n);
    for (size_t i = 0; i < n; i++) {
        r[i].used = 1;
        r[i].addr[0] = (uint8_t)(i + 1);
        r[i].name[0] = 'd';
        r[i].name[1] = (char)('0' + i);
    }
}

void test_current_size_is_decoded(void) {
    reg_rec_t in[REG_SLOTS], out[REG_SLOTS];
    make(in, REG_SLOTS);
    TEST_ASSERT_TRUE(registry_decode(in, sizeof(in), out));
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));
}

/** A registry written by firmware up to 1.3.x holds REG_SLOTS_V1 records: they are kept, the rest is empty. */
void test_older_smaller_blob_is_migrated(void) {
    reg_rec_t in[REG_SLOTS_V1], out[REG_SLOTS];
    make(in, REG_SLOTS_V1);
    TEST_ASSERT_TRUE(registry_decode(in, sizeof(in), out));
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));
    for (size_t i = REG_SLOTS_V1; i < REG_SLOTS; i++) {
        TEST_ASSERT_EQUAL(0, out[i].used);
    }
}

/** A registry written by a firmware with more slots must not be thrown away: the first REG_SLOTS are kept. */
void test_newer_larger_blob_is_truncated_not_rejected(void) {
    reg_rec_t in[REG_SLOTS + 3], out[REG_SLOTS];
    make(in, REG_SLOTS + 3);
    TEST_ASSERT_TRUE(registry_decode(in, sizeof(in), out));
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(out));
}

void test_partial_record_is_rejected(void) {
    reg_rec_t in[REG_SLOTS], out[REG_SLOTS];
    make(in, REG_SLOTS);
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_FALSE(registry_decode(in, sizeof(reg_rec_t) * 2 + 1, out));
    for (size_t i = 0; i < REG_SLOTS; i++) {
        TEST_ASSERT_EQUAL(0, out[i].used); /* A rejected blob leaves an empty registry, never garbage. */
    }
}

void test_empty_and_null_are_rejected(void) {
    reg_rec_t in[REG_SLOTS], out[REG_SLOTS];
    make(in, REG_SLOTS);
    TEST_ASSERT_FALSE(registry_decode(in, 0, out));
    TEST_ASSERT_FALSE(registry_decode(NULL, sizeof(in), out));
}

void test_names_are_terminated(void) {
    reg_rec_t in[REG_SLOTS], out[REG_SLOTS];
    make(in, REG_SLOTS);
    memset(in[0].name, 'x', REG_NAME_MAX); /* No NUL at all. */
    TEST_ASSERT_TRUE(registry_decode(in, sizeof(in), out));
    TEST_ASSERT_EQUAL('\0', out[0].name[REG_NAME_MAX - 1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_current_size_is_decoded);
    RUN_TEST(test_older_smaller_blob_is_migrated);
    RUN_TEST(test_newer_larger_blob_is_truncated_not_rejected);
    RUN_TEST(test_partial_record_is_rejected);
    RUN_TEST(test_empty_and_null_are_rejected);
    RUN_TEST(test_names_are_terminated);
    return UNITY_END();
}
