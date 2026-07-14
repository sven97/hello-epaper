#include <unity.h>
#include "logic/layout_math.h"

void setUp() {}
void tearDown() {}

// The bug this guards against: line spacing that's narrower than the font
// actually drawn at that spacing causes overlapping text. lineHeightFor()
// must always leave more room than the font's rendered height.
static void assertLineHeightFitsFont(int textSize) {
    int h = lineHeightFor(textSize);
    TEST_ASSERT_TRUE(h > FONT4_NATIVE_PX * textSize);
}

void test_line_height_always_exceeds_font_height() {
    assertLineHeightFitsFont(1);
    assertLineHeightFitsFont(2);
}

// Sanity only (non-negative, ordered, no crash-inducing values) -- NOT a
// fit guarantee. Panels this small (well under anything in
// docs/panel-combos.md) are a documented, accepted gap (see
// docs/hardware-checklist.md's compact-layout note). The fits-asserters
// below are the real fit guarantee, applied to the panels this firmware
// actually ships.
static void assertSane(int width, int height) {
    LayoutMetrics m = computeLayout(width, height);
    TEST_ASSERT_TRUE(m.lineH > 0);
    TEST_ASSERT_TRUE(m.smallLineH > 0);
    TEST_ASSERT_TRUE(m.titleY > 0);
    TEST_ASSERT_TRUE(m.subtitleY > m.titleY);
    TEST_ASSERT_TRUE(m.bodySize >= 1);
    TEST_ASSERT_TRUE(m.smallSize >= 1);
    TEST_ASSERT_TRUE(m.qrScale >= 1 && m.qrScale <= 4);
    TEST_ASSERT_TRUE(m.provQrScale >= 1 && m.provQrScale <= 4);
    TEST_ASSERT_TRUE(m.provTitleY >= 0);
    TEST_ASSERT_TRUE(m.provChangeY >= 0);
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

// Portrait: one column, everything stacked sequentially top to bottom --
// title, subtitle, 3 info lines, caption, url, QR, 3 legend lines. The real
// fit guarantee is that the whole sequence stays on-panel and each block's
// natural ordering holds (it always does by construction; what can actually
// break is the final element running past the bottom edge).
static void assertPortraitFits(int width, int height) {
    LayoutMetrics m = computeLayout(width, height);
    TEST_ASSERT_FALSE(m.landscape);
    TEST_ASSERT_TRUE(m.titleY < m.subtitleY);
    TEST_ASSERT_TRUE(m.subtitleY < m.info0Y);
    TEST_ASSERT_TRUE(m.info0Y < m.info1Y);
    TEST_ASSERT_TRUE(m.info1Y < m.info2Y);
    TEST_ASSERT_TRUE(m.info2Y < m.captionY);
    TEST_ASSERT_TRUE(m.captionY < m.urlY);
    TEST_ASSERT_TRUE(m.urlY < m.qrCy);
    TEST_ASSERT_TRUE(m.qrCy < m.legend0Y);
    TEST_ASSERT_TRUE(m.legend0Y < m.legend1Y);
    TEST_ASSERT_TRUE(m.legend1Y < m.legend2Y);
    TEST_ASSERT_TRUE(m.legend2Y + m.chromeLineH / 2 < height);
}

// Landscape: two independent columns (left: title/subtitle/info; right:
// caption/QR/url/legend). Each column's own ordering holds by construction;
// the real fit guarantee is that both columns' last element stays on-panel.
static void assertLandscapeFits(int width, int height) {
    LayoutMetrics m = computeLayout(width, height);
    TEST_ASSERT_TRUE(m.landscape);
    TEST_ASSERT_TRUE(m.leftCx < m.rightCx);
    TEST_ASSERT_TRUE(m.titleY < m.subtitleY);
    TEST_ASSERT_TRUE(m.subtitleY < m.info0Y);
    TEST_ASSERT_TRUE(m.info0Y < m.info1Y);
    TEST_ASSERT_TRUE(m.info1Y < m.info2Y);
    TEST_ASSERT_TRUE(m.info2Y + m.lineH / 2 < height);
    TEST_ASSERT_TRUE(m.captionY < m.qrCy);
    TEST_ASSERT_TRUE(m.qrCy < m.urlY);
    TEST_ASSERT_TRUE(m.urlY < m.legend0Y);
    TEST_ASSERT_TRUE(m.legend0Y < m.legend1Y);
    TEST_ASSERT_TRUE(m.legend1Y < m.legend2Y);
    TEST_ASSERT_TRUE(m.legend2Y + m.chromeLineH / 2 < height);
}

void test_status_screen_fits_ee02() { assertPortraitFits(1200, 1600); }
void test_status_screen_fits_ee02_landscape() { assertLandscapeFits(1600, 1200); }
void test_status_screen_fits_ee03() { assertLandscapeFits(1872, 1404); }
void test_status_screen_fits_ee03_portrait() { assertPortraitFits(1404, 1872); }
void test_status_screen_fits_ee04_default() { assertLandscapeFits(800, 480); }
void test_status_screen_fits_ee04_portrait() { assertPortraitFits(480, 800); }

// The provisioning screen draws 4 body lines + 3 caption lines + 2 QR
// codes, stacked top-down by computeLayout() itself (provTitleY..
// provChangeY); the real fit guarantee is just that the last anchor plus
// its own line height stays on-panel.
static void assertProvisioningScreenFits(int width, int height) {
    LayoutMetrics m = computeLayout(width, height);
    TEST_ASSERT_TRUE(m.provChangeY + m.smallLineH < height);
}

void test_provisioning_screen_fits_ee02() { assertProvisioningScreenFits(1200, 1600); }
void test_provisioning_screen_fits_ee02_landscape() { assertProvisioningScreenFits(1600, 1200); }
void test_provisioning_screen_fits_ee03() { assertProvisioningScreenFits(1872, 1404); }
void test_provisioning_screen_fits_ee03_portrait() { assertProvisioningScreenFits(1404, 1872); }
void test_provisioning_screen_fits_ee04_default() { assertProvisioningScreenFits(800, 480); }
void test_provisioning_screen_fits_ee04_portrait() { assertProvisioningScreenFits(480, 800); }

void test_no_overflow_400x300() { assertSane(400, 300); }
void test_no_overflow_200x200() { assertSane(200, 200); }

void test_small_panel_uses_smaller_body_text() {
    TEST_ASSERT_EQUAL(1, computeLayout(200, 200).bodySize);
    TEST_ASSERT_EQUAL(1, computeLayout(400, 300).bodySize);
    TEST_ASSERT_EQUAL(1, computeLayout(800, 480).bodySize); // below the 600px tier
    TEST_ASSERT_EQUAL(2, computeLayout(1200, 1600).bodySize);
}

// Font tiers: large panels get the big FreeSans faces, small panels the
// smaller ones -- and the small tier's chrome text (16px) must stay
// smaller than its own subtitle (22px), matching the "chrome text smaller
// than subtitle" requirement on every tier, not just the large one.
void test_font_tiers_scale_together() {
    LayoutMetrics large = computeLayout(1200, 1600);
    TEST_ASSERT_EQUAL(TITLE_PX_LARGE, large.titleH);
    TEST_ASSERT_EQUAL(SUB_PX_LARGE, large.subH);
    TEST_ASSERT_EQUAL(CHROME_PX_LARGE, large.chromeH);
    TEST_ASSERT_TRUE(large.chromeH < large.subH);
    TEST_ASSERT_TRUE(large.subH < large.titleH);

    LayoutMetrics small = computeLayout(800, 480);
    TEST_ASSERT_EQUAL(TITLE_PX_SMALL, small.titleH);
    TEST_ASSERT_EQUAL(SUB_PX_SMALL, small.subH);
    TEST_ASSERT_EQUAL(CHROME_PX_SMALL, small.chromeH);
    TEST_ASSERT_TRUE(small.chromeH < small.subH);
    TEST_ASSERT_TRUE(small.subH < small.titleH);
}

// Landscape (width > height) vs. portrait is a property of the panel's
// current orientation, not the board -- EE04/EE05 landscape gets the
// two-column layout, but their own rotated (portrait) orientation doesn't.
void test_landscape_flag_matches_orientation() {
    TEST_ASSERT_FALSE(computeLayout(1200, 1600).landscape);
    TEST_ASSERT_TRUE(computeLayout(1600, 1200).landscape);
    TEST_ASSERT_TRUE(computeLayout(800, 480).landscape);
    TEST_ASSERT_FALSE(computeLayout(480, 800).landscape);
}

// Provisioning QR scale must shrink (not overlap text) on the panel this
// bug was found on, and stay at the original fixed size on large panels.
void test_provisioning_qr_scale_adapts() {
    TEST_ASSERT_EQUAL(4, computeLayout(1200, 1600).provQrScale);
    TEST_ASSERT_EQUAL(4, computeLayout(1872, 1404).provQrScale);
    TEST_ASSERT_TRUE(computeLayout(800, 480).provQrScale < 4);
}

// The two-column landscape layout is what rescues EE04/EE05's tight
// 800x480 panel -- it should get the QR at full size (using the panel's
// spare width instead of fighting for scarce height), unlike the old
// single-column layout which had to shrink the QR down to scale 1-2 there.
void test_landscape_status_qr_full_size_on_tight_panel() {
    TEST_ASSERT_EQUAL(4, computeLayout(800, 480).qrScale);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_line_height_always_exceeds_font_height);
    RUN_TEST(test_status_screen_fits_ee02);
    RUN_TEST(test_status_screen_fits_ee02_landscape);
    RUN_TEST(test_status_screen_fits_ee03);
    RUN_TEST(test_status_screen_fits_ee03_portrait);
    RUN_TEST(test_status_screen_fits_ee04_default);
    RUN_TEST(test_status_screen_fits_ee04_portrait);
    RUN_TEST(test_provisioning_screen_fits_ee02);
    RUN_TEST(test_provisioning_screen_fits_ee02_landscape);
    RUN_TEST(test_provisioning_screen_fits_ee03);
    RUN_TEST(test_provisioning_screen_fits_ee03_portrait);
    RUN_TEST(test_provisioning_screen_fits_ee04_default);
    RUN_TEST(test_provisioning_screen_fits_ee04_portrait);
    RUN_TEST(test_no_overflow_400x300);
    RUN_TEST(test_no_overflow_200x200);
    RUN_TEST(test_small_panel_uses_smaller_body_text);
    RUN_TEST(test_font_tiers_scale_together);
    RUN_TEST(test_landscape_flag_matches_orientation);
    RUN_TEST(test_provisioning_qr_scale_adapts);
    RUN_TEST(test_landscape_status_qr_full_size_on_tight_panel);
    return UNITY_END();
}
