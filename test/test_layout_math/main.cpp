#include <unity.h>
#include <cstring>
#include "logic/layout_math.h"

void setUp() {}
void tearDown() {}

// ---- fitSize --------------------------------------------------------
static int wideMeasure(SizeRole, int sizePx, const char *text) {
    // 1px per size-unit per character -- deterministic, no real font needed.
    return (int)strlen(text) * sizePx;
}

void test_fitSize_picks_largest_rung_that_fits() {
    FitItem items[] = { {"hi", 100} }; // 56px: 2*56=112>100 fails; 42px: 84<=100 fits
    int size = fitSize(SizeRole::Title, items, 1, wideMeasure);
    TEST_ASSERT_EQUAL(42, size);
}

void test_fitSize_falls_back_to_smallest_rung_when_nothing_fits() {
    FitItem items[] = { {"a very long string that never fits", 5} };
    int size = fitSize(SizeRole::Chrome, items, 1, wideMeasure);
    TEST_ASSERT_EQUAL(CHROME_SIZES[CHROME_SIZES_N - 1], size);
}

void test_fitSize_tightest_item_governs() {
    // "short" fits comfortably at every rung (widthPx generous); the long
    // item fails at every rung (widthPx too small even at the floor) --
    // fitSize must fall back to the floor because ONE line failing fails
    // the whole role, even though the other line would fit fine.
    FitItem items[] = { {"short", 300}, {"a very long string indeed", 60} };
    int size = fitSize(SizeRole::Stat, items, 2, wideMeasure);
    TEST_ASSERT_EQUAL(STAT_SIZES[STAT_SIZES_N - 1], size);
}

// ---- shrinkOneStep: fixed priority (chrome, then stat, then title) ----
void test_shrink_order_chrome_first() {
    RoleSizes sizes{TITLE_SIZES[0], STAT_SIZES[0], CHROME_SIZES[0]};
    TEST_ASSERT_TRUE(shrinkOneStep(sizes));
    TEST_ASSERT_EQUAL(TITLE_SIZES[0], sizes.title);
    TEST_ASSERT_EQUAL(STAT_SIZES[0], sizes.stat);
    TEST_ASSERT_EQUAL(CHROME_SIZES[1], sizes.chrome);
}

void test_shrink_moves_to_stat_once_chrome_is_floored() {
    RoleSizes sizes{TITLE_SIZES[0], STAT_SIZES[0], CHROME_SIZES[CHROME_SIZES_N - 1]};
    TEST_ASSERT_TRUE(shrinkOneStep(sizes));
    TEST_ASSERT_EQUAL(STAT_SIZES[1], sizes.stat);
    TEST_ASSERT_EQUAL(CHROME_SIZES[CHROME_SIZES_N - 1], sizes.chrome); // unchanged
}

void test_shrink_title_last_and_stops_at_floor() {
    RoleSizes sizes{TITLE_SIZES[0], STAT_SIZES[STAT_SIZES_N - 1], CHROME_SIZES[CHROME_SIZES_N - 1]};
    TEST_ASSERT_TRUE(shrinkOneStep(sizes));
    TEST_ASSERT_EQUAL(TITLE_SIZES[1], sizes.title);

    RoleSizes floored{TITLE_SIZES[TITLE_SIZES_N - 1], STAT_SIZES[STAT_SIZES_N - 1], CHROME_SIZES[CHROME_SIZES_N - 1]};
    TEST_ASSERT_FALSE(shrinkOneStep(floored));
}

// ---- computeFit ---------------------------------------------------------
void test_computeFit_fits_generous_panel() {
    ContentShape shape{};
    shape.statCount = 3; shape.hasQr = true; shape.hasScan = true;
    shape.hasUrl = true; shape.legendCount = 3;
    RoleSizes sizes{56, 42, 29};
    FitResult fit = computeFit(shape, sizes, 1600, 1000);
    TEST_ASSERT_TRUE(fit.totalPx <= 1600);
    TEST_ASSERT_TRUE(fit.qrScale >= QR_MIN_SCALE);
    TEST_ASSERT_FALSE(fit.qrTight);
}

void test_computeFit_qr_tight_on_narrow_panel() {
    ContentShape shape{};
    shape.hasQr = true;
    RoleSizes sizes{8, 8, 8};
    FitResult fit = computeFit(shape, sizes, 2000, 60); // very narrow row
    TEST_ASSERT_TRUE(fit.qrTight);
}

void test_computeFit_no_qr_has_no_qr_budget() {
    ContentShape shape{}; // everything false/0, no QR
    RoleSizes sizes{8, 8, 8};
    FitResult fit = computeFit(shape, sizes, 200, 200);
    TEST_ASSERT_FALSE(fit.hasQr);
    TEST_ASSERT_EQUAL(0, fit.qrScale);
    TEST_ASSERT_TRUE(fit.totalPx == fit.fixedPx);
}

// ---- configForLevel: cascade applies cumulatively, in fixed order ----
void test_configForLevel_zero_is_full_content() {
    ContentConfig cfg = configForLevel(0);
    TEST_ASSERT_TRUE(cfg.legendMode == LegendMode::Full);
    TEST_ASSERT_TRUE(cfg.captionMode == CaptionMode::Full);
    TEST_ASSERT_TRUE(cfg.showUrl && cfg.showNextStat && cfg.showVersion &&
                     cfg.showScan && cfg.statDetail && cfg.showQr);
}

void test_configForLevel_cascade_order() {
    TEST_ASSERT_TRUE(configForLevel(1).legendMode == LegendMode::Combined);
    TEST_ASSERT_TRUE(configForLevel(2).legendMode == LegendMode::None);
    TEST_ASSERT_FALSE(configForLevel(3).statDetail);
    TEST_ASSERT_TRUE(configForLevel(4).captionMode == CaptionMode::Short);
    TEST_ASSERT_FALSE(configForLevel(5).showUrl);
    TEST_ASSERT_FALSE(configForLevel(6).showNextStat);
    TEST_ASSERT_FALSE(configForLevel(7).showVersion);
    TEST_ASSERT_TRUE(configForLevel(8).captionMode == CaptionMode::None);
    TEST_ASSERT_FALSE(configForLevel(9).showScan);
    TEST_ASSERT_FALSE(configForLevel(10).showQr);
    // Every earlier reduction stays applied at later levels (cumulative).
    TEST_ASSERT_TRUE(configForLevel(10).legendMode == LegendMode::None);
    TEST_ASSERT_FALSE(configForLevel(10).statDetail);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fitSize_picks_largest_rung_that_fits);
    RUN_TEST(test_fitSize_falls_back_to_smallest_rung_when_nothing_fits);
    RUN_TEST(test_fitSize_tightest_item_governs);
    RUN_TEST(test_shrink_order_chrome_first);
    RUN_TEST(test_shrink_moves_to_stat_once_chrome_is_floored);
    RUN_TEST(test_shrink_title_last_and_stops_at_floor);
    RUN_TEST(test_computeFit_fits_generous_panel);
    RUN_TEST(test_computeFit_qr_tight_on_narrow_panel);
    RUN_TEST(test_computeFit_no_qr_has_no_qr_budget);
    RUN_TEST(test_configForLevel_zero_is_full_content);
    RUN_TEST(test_configForLevel_cascade_order);
    return UNITY_END();
}
