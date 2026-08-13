#include <unity.h>
#include "logic/ring_log.h"

void setUp() {}
void tearDown() {}

void test_empty_snapshot() {
    RingLog log(8);
    TEST_ASSERT_EQUAL_STRING("", log.snapshot().c_str());
}

void test_snapshot_before_wrap() {
    RingLog log(8);
    log.append((const uint8_t *)"abc", 3);
    TEST_ASSERT_EQUAL_STRING("abc", log.snapshot().c_str());
}

void test_snapshot_after_wrap_keeps_chronological_order() {
    RingLog log(4);
    log.append((const uint8_t *)"abcdef", 6); // wraps: only "cdef" fits
    TEST_ASSERT_EQUAL_STRING("cdef", log.snapshot().c_str());
}

void test_multiple_appends_wrap_correctly() {
    RingLog log(4);
    log.append((const uint8_t *)"ab", 2);
    log.append((const uint8_t *)"cd", 2); // buffer now full: "abcd"
    log.append((const uint8_t *)"ef", 2); // overwrites oldest 2 -> "cdef"
    TEST_ASSERT_EQUAL_STRING("cdef", log.snapshot().c_str());
}

void test_single_write_larger_than_capacity_keeps_tail() {
    RingLog log(3);
    log.append((const uint8_t *)"hello", 5); // only the last 3 bytes matter
    TEST_ASSERT_EQUAL_STRING("llo", log.snapshot().c_str());
}

void test_capacity_reports_constructor_value() {
    RingLog log(4096);
    TEST_ASSERT_EQUAL_UINT32(4096, (uint32_t)log.capacity());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_snapshot);
    RUN_TEST(test_snapshot_before_wrap);
    RUN_TEST(test_snapshot_after_wrap_keeps_chronological_order);
    RUN_TEST(test_multiple_appends_wrap_correctly);
    RUN_TEST(test_single_write_larger_than_capacity_keeps_tail);
    RUN_TEST(test_capacity_reports_constructor_value);
    return UNITY_END();
}
