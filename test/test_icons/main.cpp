#include <unity.h>
#include "icons.h"

void setUp() {}
void tearDown() {}

void test_icon_dimensions_are_48x48() {
    TEST_ASSERT_EQUAL(48, ICON_W);
    TEST_ASSERT_EQUAL(48, ICON_H);
}

void test_icon_arrays_are_288_bytes() {
    TEST_ASSERT_EQUAL(288, sizeof(ICON_BATTERY_FULL));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_BATTERY_MEDIUM));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_BATTERY_LOW));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_BATTERY_EMPTY));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_WIFI_FULL));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_WIFI_MEDIUM));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_WIFI_LOW));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_WIFI_NONE));
    TEST_ASSERT_EQUAL(288, sizeof(ICON_NEXT));
}

// Sanity, not a full pixel audit: the battery outline's top bar (rows
// 10-11 -- doubled from row 5 of the pre-upscale 24x24 source) must be
// identical across all four battery variants (it's the shared outline,
// not the fill) and non-blank, and battery-full must have more set bits
// than battery-low (more of the fill bars are drawn).
static int popcount288(const uint8_t *bmp) {
    int n = 0;
    for (int i = 0; i < 288; i++)
        for (int b = 0; b < 8; b++)
            if (bmp[i] & (1 << b)) n++;
    return n;
}

void test_battery_outline_row_is_set() {
    const int rowBytes = 6, row = 10;
    const uint8_t *full = ICON_BATTERY_FULL + row * rowBytes;
    bool anySet = false;
    for (int i = 0; i < rowBytes; i++) {
        if (full[i] != 0) anySet = true;
        TEST_ASSERT_EQUAL(full[i], ICON_BATTERY_MEDIUM[row * rowBytes + i]);
        TEST_ASSERT_EQUAL(full[i], ICON_BATTERY_LOW[row * rowBytes + i]);
        TEST_ASSERT_EQUAL(full[i], ICON_BATTERY_EMPTY[row * rowBytes + i]);
    }
    TEST_ASSERT_TRUE(anySet);
}

void test_battery_fill_scales_with_level() {
    int full = popcount288(ICON_BATTERY_FULL);
    int medium = popcount288(ICON_BATTERY_MEDIUM);
    int low = popcount288(ICON_BATTERY_LOW);
    int empty = popcount288(ICON_BATTERY_EMPTY);
    TEST_ASSERT_TRUE(full > medium);
    TEST_ASSERT_TRUE(medium > low);
    TEST_ASSERT_TRUE(low > empty);
}

// Unlike the battery family, wifi bars aren't a strict superset series --
// each state redraws all three bars, and a higher tier's bar can shift
// footprint (not just toggle solid/hollow) as long as the overall silhouette
// still reads as "more bars lit". So only the unambiguous endpoints are
// checked here, not step-by-step monotonicity.
void test_wifi_full_has_more_fill_than_none() {
    int full = popcount288(ICON_WIFI_FULL);
    int none = popcount288(ICON_WIFI_NONE);
    TEST_ASSERT_TRUE(full > none);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_icon_dimensions_are_48x48);
    RUN_TEST(test_icon_arrays_are_288_bytes);
    RUN_TEST(test_battery_outline_row_is_set);
    RUN_TEST(test_battery_fill_scales_with_level);
    RUN_TEST(test_wifi_full_has_more_fill_than_none);
    return UNITY_END();
}
