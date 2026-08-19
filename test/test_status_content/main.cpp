#include <unity.h>
#include <cstring>
#include "logic/status_content.h"

void setUp() {}
void tearDown() {}

// Deterministic fake measurement -- no real font needed. Roughly
// character-count * size, close enough to a real font's average advance
// width to exercise the fitting algorithm meaningfully.
static int fakeMeasure(SizeRole, int sizePx, const char *text) {
    return (int)(strlen(text) * sizePx * 0.6f);
}

static ScreenContent normalContent() {
    ScreenContent c{};
    c.batteryPct = 98;
    strncpy(c.batteryVoltage, "4.17V", sizeof(c.batteryVoltage) - 1);
    strncpy(c.wifiBase, "strong", sizeof(c.wifiBase) - 1);
    strncpy(c.wifiSsid, "FUIYOH", sizeof(c.wifiSsid) - 1);
    strncpy(c.nextBase, "16:32", sizeof(c.nextBase) - 1);
    strncpy(c.lastFetch, "last Wed 14:32", sizeof(c.lastFetch) - 1);
    strncpy(c.settingsUrl, "http://ee02.local", sizeof(c.settingsUrl) - 1);
    return c;
}

void test_buildLines_normal_full_content() {
    ContentConfig cfg = configForLevel(0);
    ContentLine lines[MAX_CONTENT_LINES];
    ScreenContent c = normalContent();
    // title, board, battery, wifi, next (3 stats), scan, url, legend x3 = 10
    int n = buildLines(ScreenState::Normal, cfg, c, "EE02", "abc123",
                       "EE02-Setup", lines, MAX_CONTENT_LINES);
    TEST_ASSERT_EQUAL(10, n);
    TEST_ASSERT_TRUE(lines[0].kind == LineKind::Title);
    TEST_ASSERT_TRUE(strstr(lines[0].text, "ver.abc123") != nullptr);
    TEST_ASSERT_TRUE(lines[1].kind == LineKind::Board);
    TEST_ASSERT_TRUE(strstr(lines[1].text, "EE02") != nullptr);
    TEST_ASSERT_TRUE(lines[2].icon == StatIcon::Battery);
    TEST_ASSERT_TRUE(strstr(lines[2].text, "98%") != nullptr);
    TEST_ASSERT_TRUE(strstr(lines[2].text, "4.17V") != nullptr); // detail present at level 0
}

void test_buildLines_legend_combined_and_dropped() {
    ContentLine lines[MAX_CONTENT_LINES];
    ScreenContent c = normalContent();

    ContentConfig full = configForLevel(0);
    int nFull = buildLines(ScreenState::Normal, full, c, "EE02", "abc123", "EE02-Setup", lines, MAX_CONTENT_LINES);
    int legendFull = 0;
    for (int i = 0; i < nFull; i++) if (lines[i].kind == LineKind::Legend) legendFull++;
    TEST_ASSERT_EQUAL(3, legendFull);

    ContentConfig combined = configForLevel(1);
    int nCombined = buildLines(ScreenState::Normal, combined, c, "EE02", "abc123", "EE02-Setup", lines, MAX_CONTENT_LINES);
    int legendCombined = 0;
    for (int i = 0; i < nCombined; i++) if (lines[i].kind == LineKind::Legend) legendCombined++;
    TEST_ASSERT_EQUAL(1, legendCombined);
    TEST_ASSERT_EQUAL(nFull - 2, nCombined); // 3 rows collapsed to 1

    ContentConfig none = configForLevel(2);
    int nNone = buildLines(ScreenState::Normal, none, c, "EE02", "abc123", "EE02-Setup", lines, MAX_CONTENT_LINES);
    int legendNone = 0;
    for (int i = 0; i < nNone; i++) if (lines[i].kind == LineKind::Legend) legendNone++;
    TEST_ASSERT_EQUAL(0, legendNone);
}

void test_buildLines_stat_detail_dropped_at_level3() {
    ContentConfig cfg = configForLevel(3); // statDetail=false
    ContentLine lines[MAX_CONTENT_LINES];
    ScreenContent c = normalContent();
    int n = buildLines(ScreenState::Normal, cfg, c, "EE02", "abc123", "EE02-Setup", lines, MAX_CONTENT_LINES);
    for (int i = 0; i < n; i++) {
        if (lines[i].kind == LineKind::Stat)
            TEST_ASSERT_TRUE(strstr(lines[i].text, "\xC2\xB7") == nullptr); // no detail separator
    }
}

void test_buildLines_onboarding_uses_ap_name() {
    ContentConfig cfg = configForLevel(0);
    ContentLine lines[MAX_CONTENT_LINES];
    ScreenContent c{};
    c.batteryPct = 98;
    strncpy(c.wifiBase, "--", sizeof(c.wifiBase) - 1);
    strncpy(c.nextBase, "--", sizeof(c.nextBase) - 1);
    int n = buildLines(ScreenState::Onboarding, cfg, c, "EE02", "abc123", "EE02-Setup", lines, MAX_CONTENT_LINES);
    bool foundCaption = false;
    for (int i = 0; i < n; i++) {
        if (lines[i].kind == LineKind::Caption) {
            foundCaption = true;
            TEST_ASSERT_TRUE(strstr(lines[i].text, "EE02-Setup") != nullptr);
        }
    }
    TEST_ASSERT_TRUE(foundCaption);
}

void test_buildLines_error_uses_error_message() {
    ContentConfig cfg = configForLevel(0);
    ContentLine lines[MAX_CONTENT_LINES];
    ScreenContent c = normalContent();
    strncpy(c.errorMsg, "image server said HTTP 404", sizeof(c.errorMsg) - 1);
    int n = buildLines(ScreenState::Error, cfg, c, "EE02", "abc123", "EE02-Setup", lines, MAX_CONTENT_LINES);
    bool foundScan = false;
    for (int i = 0; i < n; i++) {
        if (lines[i].kind == LineKind::Scan) {
            foundScan = true;
            TEST_ASSERT_TRUE(strstr(lines[i].text, "HTTP 404") != nullptr);
        }
    }
    TEST_ASSERT_TRUE(foundScan);
}

// ---- fitScreen: always terminates, always produces usable content ----
void test_fitScreen_ee02_portrait_fits_at_level_zero() {
    ScreenContent c = normalContent();
    ScreenFit fit = fitScreen(ScreenState::Normal, c, "EE02", "abc123", "EE02-Setup",
                              1200, 1600, fakeMeasure);
    TEST_ASSERT_TRUE(fit.lineCount > 0);
    TEST_ASSERT_TRUE(fit.qrScale >= 1 && fit.qrScale <= 4);
    TEST_ASSERT_EQUAL(0, fit.level); // generous panel: no reduction needed
}

void test_fitScreen_tiny_panel_terminates_and_keeps_header() {
    ScreenContent c = normalContent();
    ScreenFit fit = fitScreen(ScreenState::Normal, c, "EE02", "abc123", "EE02-Setup",
                              128, 296, fakeMeasure);
    TEST_ASSERT_TRUE(fit.lineCount > 0);
    TEST_ASSERT_TRUE(fit.level >= 0 && fit.level <= REDUCTION_LEVELS);
    bool hasTitle = false;
    for (int i = 0; i < fit.lineCount; i++)
        if (fit.lines[i].line.kind == LineKind::Title) hasTitle = true;
    TEST_ASSERT_TRUE(hasTitle); // title is never in the reduction cascade
}

void test_fitScreen_qr_never_below_floor_unless_dropped() {
    ScreenContent c = normalContent();
    ScreenFit fit = fitScreen(ScreenState::Normal, c, "EE02", "abc123", "EE02-Setup",
                              128, 296, fakeMeasure);
    if (fit.qrScale > 0) TEST_ASSERT_TRUE(fit.qrScale >= QR_MIN_SCALE);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_buildLines_normal_full_content);
    RUN_TEST(test_buildLines_legend_combined_and_dropped);
    RUN_TEST(test_buildLines_stat_detail_dropped_at_level3);
    RUN_TEST(test_buildLines_onboarding_uses_ap_name);
    RUN_TEST(test_buildLines_error_uses_error_message);
    RUN_TEST(test_fitScreen_ee02_portrait_fits_at_level_zero);
    RUN_TEST(test_fitScreen_tiny_panel_terminates_and_keeps_header);
    RUN_TEST(test_fitScreen_qr_never_below_floor_unless_dropped);
    return UNITY_END();
}
