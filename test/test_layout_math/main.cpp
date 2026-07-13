#include <unity.h>
#include "logic/layout_math.h"

void setUp() {}
void tearDown() {}

// Regression-pin: must reproduce every constant the status/provisioning
// screens used to hardcode for the EE02 panel (1200x1600 portrait).
void test_matches_ee02_constants() {
    LayoutMetrics m = computeLayout(1200, 1600);
    TEST_ASSERT_EQUAL(90, m.lineH);
    TEST_ASSERT_EQUAL(55, m.smallLineH);
    TEST_ASSERT_EQUAL(1400, m.legendTop);
    TEST_ASSERT_EQUAL(2, m.bodySize);
    TEST_ASSERT_EQUAL(1, m.smallSize);
    TEST_ASSERT_EQUAL(90, m.provTitleY);
    TEST_ASSERT_EQUAL(210, m.provStep1Y);
    TEST_ASSERT_EQUAL(400, m.provQr1Y);
    TEST_ASSERT_EQUAL(545, m.provJoinManualY);
    TEST_ASSERT_EQUAL(660, m.provStep2Y);
    TEST_ASSERT_EQUAL(725, m.provQrHintY);
    TEST_ASSERT_EQUAL(880, m.provQr2Y);
    TEST_ASSERT_EQUAL(1060, m.provStep3Y);
    TEST_ASSERT_EQUAL(1140, m.provChangeY);
}

static void assertSane(int width, int height) {
    LayoutMetrics m = computeLayout(width, height);
    TEST_ASSERT_TRUE(m.lineH > 0);
    TEST_ASSERT_TRUE(m.smallLineH > 0);
    TEST_ASSERT_TRUE(m.legendTop > 0 && m.legendTop < height);
    TEST_ASSERT_TRUE(m.bodySize >= 1);
    TEST_ASSERT_TRUE(m.smallSize >= 1);
    TEST_ASSERT_TRUE(m.provTitleY >= 0 && m.provTitleY < height);
    TEST_ASSERT_TRUE(m.provChangeY >= 0 && m.provChangeY < height);
    // Provisioning anchors stay in top-to-bottom order regardless of size.
    TEST_ASSERT_TRUE(m.provTitleY < m.provStep1Y);
    TEST_ASSERT_TRUE(m.provStep1Y < m.provQr1Y);
    TEST_ASSERT_TRUE(m.provQr1Y < m.provJoinManualY);
    TEST_ASSERT_TRUE(m.provJoinManualY < m.provStep2Y);
    TEST_ASSERT_TRUE(m.provStep2Y < m.provQrHintY);
    TEST_ASSERT_TRUE(m.provQrHintY < m.provQr2Y);
    TEST_ASSERT_TRUE(m.provQr2Y < m.provStep3Y);
    TEST_ASSERT_TRUE(m.provStep3Y < m.provChangeY);
}

void test_no_overflow_ee04_default() { assertSane(800, 480); }
void test_no_overflow_400x300() { assertSane(400, 300); }
void test_no_overflow_200x200() { assertSane(200, 200); }

void test_small_panel_uses_smaller_body_text() {
    TEST_ASSERT_EQUAL(1, computeLayout(200, 200).bodySize);
    TEST_ASSERT_EQUAL(1, computeLayout(400, 300).bodySize);
    TEST_ASSERT_EQUAL(2, computeLayout(800, 480).bodySize);
    TEST_ASSERT_EQUAL(2, computeLayout(1200, 1600).bodySize);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_matches_ee02_constants);
    RUN_TEST(test_no_overflow_ee04_default);
    RUN_TEST(test_no_overflow_400x300);
    RUN_TEST(test_no_overflow_200x200);
    RUN_TEST(test_small_panel_uses_smaller_body_text);
    return UNITY_END();
}
