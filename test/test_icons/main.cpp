#include <unity.h>
#include "icons.h"

void setUp() {}
void tearDown() {}

void test_icon_dimensions_are_24x24() {
    TEST_ASSERT_EQUAL(24, ICON_W);
    TEST_ASSERT_EQUAL(24, ICON_H);
}

void test_icon_arrays_are_72_bytes() {
    TEST_ASSERT_EQUAL(72, sizeof(ICON_BATTERY_FULL));
    TEST_ASSERT_EQUAL(72, sizeof(ICON_BATTERY_MEDIUM));
    TEST_ASSERT_EQUAL(72, sizeof(ICON_BATTERY_LOW));
    TEST_ASSERT_EQUAL(72, sizeof(ICON_WIFI));
    TEST_ASSERT_EQUAL(72, sizeof(ICON_REFRESH));
}

// Sanity, not a full pixel audit: the battery outline's top bar (row 5)
// must be fully set on all three battery variants (it's the outline, not
// the fill), and battery-full must have more set bits than battery-low
// (more of the fill bars are drawn).
static int popcount72(const uint8_t *bmp) {
    int n = 0;
    for (int i = 0; i < 72; i++)
        for (int b = 0; b < 8; b++)
            if (bmp[i] & (1 << b)) n++;
    return n;
}

void test_battery_outline_row_is_set() {
    TEST_ASSERT_EQUAL(0x0F, ICON_BATTERY_FULL[5 * 3]);
    TEST_ASSERT_EQUAL(0x0F, ICON_BATTERY_MEDIUM[5 * 3]);
    TEST_ASSERT_EQUAL(0x0F, ICON_BATTERY_LOW[5 * 3]);
}

void test_battery_fill_scales_with_level() {
    int full = popcount72(ICON_BATTERY_FULL);
    int medium = popcount72(ICON_BATTERY_MEDIUM);
    int low = popcount72(ICON_BATTERY_LOW);
    TEST_ASSERT_TRUE(full > medium);
    TEST_ASSERT_TRUE(medium > low);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_icon_dimensions_are_24x24);
    RUN_TEST(test_icon_arrays_are_72_bytes);
    RUN_TEST(test_battery_outline_row_is_set);
    RUN_TEST(test_battery_fill_scales_with_level);
    return UNITY_END();
}
