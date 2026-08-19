#include <unity.h>
#include "logic/stuck_error.h"

void setUp() {}
void tearDown() {}

void test_zero_streak_never_fires() {
    TEST_ASSERT_FALSE(shouldShowStuckError(0, 600, false));
}

void test_below_threshold_does_not_fire() {
    // 35 attempts * 600 s = 21000 s < 21600 (6 h).
    TEST_ASSERT_FALSE(shouldShowStuckError(35, 600, false));
}

void test_at_threshold_fires() {
    // 36 attempts * 600 s = 21600 s == 6 h exactly.
    TEST_ASSERT_TRUE(shouldShowStuckError(36, 600, false));
}

void test_past_threshold_fires() {
    TEST_ASSERT_TRUE(shouldShowStuckError(1000, 600, false));
}

void test_already_shown_suppresses() {
    TEST_ASSERT_FALSE(shouldShowStuckError(1000, 600, true));
}

void test_scales_with_sleep_secs() {
    // A longer configured interval reaches 6 h in fewer attempts.
    TEST_ASSERT_FALSE(shouldShowStuckError(1, 3600, false)); // 1 h < 6 h
    TEST_ASSERT_TRUE(shouldShowStuckError(6, 3600, false));  // 6 h == 6 h
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_zero_streak_never_fires);
    RUN_TEST(test_below_threshold_does_not_fire);
    RUN_TEST(test_at_threshold_fires);
    RUN_TEST(test_past_threshold_fires);
    RUN_TEST(test_already_shown_suppresses);
    RUN_TEST(test_scales_with_sleep_secs);
    return UNITY_END();
}
