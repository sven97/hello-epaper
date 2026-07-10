#include <unity.h>
#include "logic/validate.h"

void setUp() {}
void tearDown() {}

void test_sleep_choices() {
    TEST_ASSERT_TRUE(isValidSleepSecs(900));
    TEST_ASSERT_TRUE(isValidSleepSecs(3600));
    TEST_ASSERT_TRUE(isValidSleepSecs(86400));
    TEST_ASSERT_FALSE(isValidSleepSecs(0));
    TEST_ASSERT_FALSE(isValidSleepSecs(3601));
}

void test_image_url() {
    TEST_ASSERT_TRUE(isValidImageUrl("https://example.com/a.jpg"));
    TEST_ASSERT_TRUE(isValidImageUrl("http://192.168.1.5/pic"));
    TEST_ASSERT_FALSE(isValidImageUrl("ftp://example.com/a.jpg"));
    TEST_ASSERT_FALSE(isValidImageUrl("https://"));
    TEST_ASSERT_FALSE(isValidImageUrl(std::string("https://") + std::string(510, 'a')));
}

void test_hours() {
    TEST_ASSERT_TRUE(isValidHour(0));
    TEST_ASSERT_TRUE(isValidHour(23));
    TEST_ASSERT_FALSE(isValidHour(24));
    TEST_ASSERT_FALSE(isValidHour(-1));
}

void test_device_name() {
    TEST_ASSERT_TRUE(isValidDeviceName("ee02"));
    TEST_ASSERT_TRUE(isValidDeviceName("living-room-frame"));
    TEST_ASSERT_FALSE(isValidDeviceName(""));
    TEST_ASSERT_FALSE(isValidDeviceName("Big Frame"));
    TEST_ASSERT_FALSE(isValidDeviceName("-frame"));
    TEST_ASSERT_FALSE(isValidDeviceName("frame-"));
    TEST_ASSERT_FALSE(isValidDeviceName("abcdefghijklmnopqrstuvwxy")); // 25
}

void test_tz_offset() {
    TEST_ASSERT_TRUE(isValidTzOffsetSec(0));
    TEST_ASSERT_TRUE(isValidTzOffsetSec(28800));    // UTC+8
    TEST_ASSERT_TRUE(isValidTzOffsetSec(-12600));   // UTC-3:30
    TEST_ASSERT_FALSE(isValidTzOffsetSec(50401));
    TEST_ASSERT_FALSE(isValidTzOffsetSec(100));     // not a 15-min step
}

void test_rotation() {
    TEST_ASSERT_TRUE(isValidRotation(0));
    TEST_ASSERT_TRUE(isValidRotation(3));
    TEST_ASSERT_FALSE(isValidRotation(4));
    TEST_ASSERT_FALSE(isValidRotation(-1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sleep_choices);
    RUN_TEST(test_image_url);
    RUN_TEST(test_hours);
    RUN_TEST(test_device_name);
    RUN_TEST(test_tz_offset);
    RUN_TEST(test_rotation);
    return UNITY_END();
}
