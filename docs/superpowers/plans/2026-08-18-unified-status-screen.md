# Unified Status Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the firmware's three unrelated screens (KEY1 status page, Wi-Fi onboarding, error page) with one shared renderer and one content-fitting engine, ported from the validated browser atlas.

**Architecture:** A pure-logic sizing/content engine (`src/logic/layout_math.h` + new `src/logic/status_content.h`, both host-testable, no Arduino dependency) decides font sizes, which content survives the reduction cascade, and the QR scale. `src/ui.cpp` gets a new `drawFrameScreen(ScreenState, ScreenContent)` that walks the engine's output and issues the real `epaper.*` draw calls. `drawStatusScreen()` (ui.cpp), `showProvisioningScreen()` (net.cpp), and `showError()` (display.cpp) keep their existing signatures and call sites but become thin wrappers: gather their own data, build a `ScreenContent`, call `drawFrameScreen()`. New pixelarticons-derived 1-bit bitmap icons (`src/icons.h`) replace the three vector-drawn icon functions. The screen always renders portrait via a new `PortraitScope` RAII guard, regardless of `settings.rotation`.

**Tech Stack:** PlatformIO / Arduino / ESP32-S3, Seeed_GFX (TFT_eSPI fork), Unity (`pio test -e native` for pure-logic tests), ricmoo/QRCode.

**Spec:** `docs/superpowers/specs/2026-08-18-unified-status-screen-design.md`

## Global Constraints

- Every size in the font ladders is a real, compiled-in firmware asset — never an invented in-between value. Title: `FreeSansBold24pt7b`(56) → `FreeSansBold18pt7b`(42) → `FreeSansBold12pt7b`(29) → `FreeSansBold9pt7b`(22) → classic Font2(16) → classic Font1/GLCD(8). Stat: `FreeSans18pt7b`(42) → `FreeSans12pt7b`(29) → `FreeSans9pt7b`(22) → classic Font2(16) → classic Font1(8). Chrome: `FreeSans12pt7b`(29) → classic Font2(16) → classic Font1(8).
- Shrink priority when the width-fitted sizes still don't stack within the panel's height is fixed: chrome first, then stat, title last — never "whichever role is currently biggest."
- QR: version 4 (33×33 modules) + ECC_LOW, 4-module quiet zone (41 total modules), 2px/module guaranteed-scannable floor (`QR_MIN_SCALE`). Below the floor, keep cutting other content or drop the QR as a last resort — never render a QR smaller than the floor.
- Content reduction cascade, in this exact order, cumulative: legend combined → legend dropped → stat detail suffix dropped → caption shortened → URL text dropped → "next" row dropped → version dropped from header → caption dropped → scan instruction dropped → QR dropped (last resort).
- The new screen always renders portrait (`PortraitScope`), regardless of `settings.rotation`; the configured orientation is restored afterward for photo display.
- Only EE02 is physically available: pure logic gets real `pio test -e native` coverage (all boards run this same code path); all four board envs (`ee02`/`ee03`/`ee04`/`ee05`) get a build check; only EE02 gets a real on-device flash + visual check. This is stated as-is in the final report, not implied away.
- Icons: pixelarticons (MIT), rasterized once to a fixed 24×24 1-bit bitmap per icon — never scaled per font tier.
- Deliberate scope decisions carried from spec approval (not gaps): the old charging-bolt indicator has no equivalent in the new design (the atlas never modeled it) and is dropped, not ported; the status screen's URL line no longer appends the last-known IP in parentheses (the atlas doesn't model this either); the reduction cascade's "legend combined" mode renders as one full-width chrome line with no keycap column (the atlas's own combined-line rendering has a regex quirk that half-parses it as a normal keyed row — not worth reproducing).

---

### Task 1: Icon bitmaps (`src/icons.h`)

**Files:**
- Create: `src/icons.h`
- Test: `test/test_icons/main.cpp`

**Interfaces:**
- Produces: `ICON_W`, `ICON_H` (both `24`), `ICON_BATTERY_FULL`/`ICON_BATTERY_MEDIUM`/`ICON_BATTERY_LOW`/`ICON_WIFI`/`ICON_REFRESH` (`const uint8_t[]`, `PROGMEM`, 72 bytes each — 24×24 1bpp, 3 bytes/row, MSB-first, matching `Adafruit_GFX`/`TFT_eSPI::drawBitmap()`'s format). Consumed by Task 4 (`display.cpp`'s `drawStatusIcon()`) and Task 5 (`ui.cpp`'s `drawFrameScreen()`).

- [ ] **Step 1: Write the failing test**

Create `test/test_icons/main.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_icons`
Expected: FAIL to compile — `icons.h` does not exist yet.

- [ ] **Step 3: Create `src/icons.h`**

```cpp
#pragma once
// Status-screen icons: hand-picked from pixelarticons
// (https://github.com/halfmage/pixelarticons, MIT license, (c) halfmage),
// rasterized once from their 24x24 SVG grid -- each of these icons is
// built entirely from axis-aligned rects, so tracing that grid directly
// to a 1-bit bitmap is exact, not a resampled/antialiased approximation.
// One fixed size for every icon (see the design spec's Icons section):
// icons render at native size regardless of the surrounding text's font
// tier. epaper.drawBitmap() draws them as 1-bit foreground-color glyphs.
//
// No Arduino dependency -- PROGMEM is a no-op off-device (this file is
// also compiled by the native unit tests), and <cstdint> covers uint8_t.
#ifndef PROGMEM
#define PROGMEM
#endif
#include <cstdint>

constexpr int ICON_W = 24;
constexpr int ICON_H = 24;

// battery-full: 24x24, 3 bytes/row, 72 bytes total
const uint8_t ICON_BATTERY_FULL[] PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x0F, 0xFF, 0xF0,
    0x0F, 0xFF, 0xF0,
    0x30, 0x00, 0x30,
    0x30, 0x00, 0x30,
    0x33, 0x33, 0x3C,
    0x33, 0x33, 0x3C,
    0x33, 0x33, 0x3C,
    0x33, 0x33, 0x3C,
    0x33, 0x33, 0x3C,
    0x33, 0x33, 0x3C,
    0x30, 0x00, 0x30,
    0x30, 0x00, 0x30,
    0x0F, 0xFF, 0xF0,
    0x0F, 0xFF, 0xF0,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

// battery-medium: 24x24, 3 bytes/row, 72 bytes total
const uint8_t ICON_BATTERY_MEDIUM[] PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x0F, 0xFF, 0xF0,
    0x0F, 0xFF, 0xF0,
    0x30, 0x00, 0x30,
    0x30, 0x00, 0x30,
    0x33, 0x30, 0x3C,
    0x33, 0x30, 0x3C,
    0x33, 0x30, 0x3C,
    0x33, 0x30, 0x3C,
    0x33, 0x30, 0x3C,
    0x33, 0x30, 0x3C,
    0x30, 0x00, 0x30,
    0x30, 0x00, 0x30,
    0x0F, 0xFF, 0xF0,
    0x0F, 0xFF, 0xF0,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

// battery-low: 24x24, 3 bytes/row, 72 bytes total
const uint8_t ICON_BATTERY_LOW[] PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x0F, 0xFF, 0xF0,
    0x0F, 0xFF, 0xF0,
    0x30, 0x00, 0x30,
    0x30, 0x00, 0x30,
    0x33, 0x00, 0x3C,
    0x33, 0x00, 0x3C,
    0x33, 0x00, 0x3C,
    0x33, 0x00, 0x3C,
    0x33, 0x00, 0x3C,
    0x33, 0x00, 0x3C,
    0x30, 0x00, 0x30,
    0x30, 0x00, 0x30,
    0x0F, 0xFF, 0xF0,
    0x0F, 0xFF, 0xF0,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

// wifi: 24x24, 3 bytes/row, 72 bytes total
const uint8_t ICON_WIFI[] PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x01, 0xFF, 0x80,
    0x01, 0xFF, 0x80,
    0x06, 0x00, 0x60,
    0x06, 0x00, 0x60,
    0x18, 0x00, 0x18,
    0x18, 0xFF, 0x18,
    0x60, 0xFF, 0x06,
    0x63, 0x00, 0xC6,
    0x03, 0x00, 0xC0,
    0x0C, 0x00, 0x30,
    0x0C, 0x7E, 0x30,
    0x00, 0x7E, 0x00,
    0x01, 0x81, 0x80,
    0x01, 0x81, 0x80,
    0x00, 0x00, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

// refresh: 24x24, 3 bytes/row, 72 bytes total
const uint8_t ICON_REFRESH[] PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x1F, 0x9E, 0x00,
    0x1F, 0x9E, 0x00,
    0x1F, 0x81, 0x80,
    0x1F, 0x81, 0x80,
    0x7F, 0xE1, 0xF8,
    0x7F, 0xE1, 0xF8,
    0x1F, 0x81, 0xF8,
    0x1F, 0x81, 0xF8,
    0x1F, 0x81, 0xF8,
    0x1F, 0x81, 0xF8,
    0x1F, 0x87, 0xFE,
    0x1F, 0x87, 0xFE,
    0x01, 0x81, 0xF8,
    0x01, 0x81, 0xF8,
    0x00, 0x79, 0xF8,
    0x00, 0x79, 0xF8,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_icons`
Expected: PASS (4 tests)

- [ ] **Step 5: Commit**

```bash
git add src/icons.h test/test_icons/main.cpp
git commit -m "icons: add pixelarticons-derived battery/wifi/refresh bitmaps"
```

---

### Task 2: Sizing engine (`src/logic/layout_math.h` rewrite)

**Files:**
- Modify: `src/logic/layout_math.h` (full rewrite — the old `computeLayout()`/`LayoutMetrics` API is removed entirely; nothing else in the tree references it after Tasks 5-7 land, and this task lands before those, so the old test file is replaced in the same step to keep the tree buildable)
- Modify: `test/test_layout_math/main.cpp` (full rewrite)

**Interfaces:**
- Produces: `SizeRole` enum (`Title`/`Stat`/`Chrome`), `TITLE_SIZES`/`STAT_SIZES`/`CHROME_SIZES` arrays + `TITLE_SIZES_N`/`STAT_SIZES_N`/`CHROME_SIZES_N`, `ICON_COL_RESERVE_FACTOR`, `LINE_HEIGHT`, `TextWidthFn` (`int(*)(SizeRole, int, const char*)`), `FitItem{const char *text; int widthPx;}`, `fitSize(SizeRole, const FitItem*, int, TextWidthFn) -> int`, `RoleSizes{int title, stat, chrome;}`, `shrinkOneStep(RoleSizes&) -> bool`, `QR_MODULES`/`QR_QUIET_MODULES`/`QR_TOTAL_MODULES`/`QR_MIN_SCALE`, `GRID_OUTER_MARGIN`/`GRID_SECTION_GAP`/`GRID_QR_GAP`, `ContentShape{int statCount; bool hasCaption, hasQr, hasScan, hasUrl; int legendCount;}`, `FitResult{float fixedPx; bool hasQr; int qrScale, qrPx; float qrGapPx; bool qrTight; float totalPx;}`, `computeFit(const ContentShape&, const RoleSizes&, int panelH, int rowW) -> FitResult`, `LegendMode` enum (`Full`/`Combined`/`None`), `CaptionMode` enum (`Full`/`Short`/`None`), `ContentConfig{LegendMode legendMode; CaptionMode captionMode; bool showUrl, showNextStat, showVersion, showScan, statDetail, showQr;}`, `configForLevel(int) -> ContentConfig`, `REDUCTION_LEVELS` (`10`). Consumed by Task 3 (`status_content.h`). This task also deletes `src/layout.h`/`src/layout.cpp` (Step 4) -- the board build is expected to fail from that step through Task 5's Step 7, since `ui.cpp`/`net.cpp` aren't rewritten to drop the old API until then. Each affected task's own steps explain what is and isn't expected to pass at that point.

- [ ] **Step 1: Write the failing test**

Replace `test/test_layout_math/main.cpp` in full:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_layout_math`
Expected: FAIL to compile — `SizeRole`, `FitItem`, `fitSize`, etc. don't exist in the old `layout_math.h` yet.

- [ ] **Step 3: Rewrite `src/logic/layout_math.h`**

```cpp
#pragma once
// Font-fitting and QR-sizing math for the unified status/onboarding/error
// screen. Pure logic: host-testable, no Arduino deps. Ported from the
// validated browser atlas (docs/superpowers/specs/2026-08-13-status-page-atlas.html)
// -- see docs/superpowers/specs/2026-08-18-unified-status-screen-design.md
// for the full design this implements.
//
// Content-line building and the top-level fitScreen() orchestration live
// in status_content.h, which includes this header; this file is only the
// role-agnostic sizing/QR/reduction-cascade math.

// ---- Font ladders: every size is a real, compiled-in firmware font
// asset (see fontFor() in ui.cpp for the size -> font-object mapping),
// largest first. Title: FreeSansBold 24/18/12/9pt, then classic
// Font2/Font1. Stat: FreeSans 18/12/9pt, then classic Font2/Font1.
// Chrome: FreeSans 12pt, then classic Font2/Font1.
constexpr int TITLE_SIZES[] = {56, 42, 29, 22, 16, 8};
constexpr int STAT_SIZES[]  = {42, 29, 22, 16, 8};
constexpr int CHROME_SIZES[] = {29, 16, 8};
constexpr int TITLE_SIZES_N = 6, STAT_SIZES_N = 5, CHROME_SIZES_N = 3;

// How much of a stat/legend row's width the shared icon/keycap column
// eats, as a multiple of that role's own font size -- the icon itself
// plus a small gap. See the atlas's "strong" -> "stro" clipping bug this
// guards against (design spec's Sizing engine section).
constexpr float ICON_COL_RESERVE_FACTOR = 1.65f;
// Line box height as a multiple of font size -- enough headroom for
// ascenders/descenders without wasting space.
constexpr float LINE_HEIGHT = 1.3f;

enum class SizeRole : uint8_t { Title, Stat, Chrome };

inline const int *ladderFor(SizeRole role, int *countOut) {
    switch (role) {
        case SizeRole::Title: *countOut = TITLE_SIZES_N; return TITLE_SIZES;
        case SizeRole::Stat:  *countOut = STAT_SIZES_N;  return STAT_SIZES;
        default:              *countOut = CHROME_SIZES_N; return CHROME_SIZES;
    }
}

// Real text-width measurement, injected so this header stays Arduino-
// free. Returns the pixel width `text` would render at, for `role`'s
// font at `sizePx` (one of that role's ladder rungs). Firmware wires
// this to epaper.textWidth() against the real font object (see
// realTextWidth() in ui.cpp); native tests use a deterministic fake.
using TextWidthFn = int (*)(SizeRole role, int sizePx, const char *text);

struct FitItem { const char *text; int widthPx; };

// Largest ladder rung at which every item fits its own available width
// -- per-item width (not one flat width) matters because stat/legend
// lines share an icon/keycap column that eats into their row. Falls
// back to the smallest rung if nothing fits (caller may still find
// individual lines tight at that floor -- flagged, not hidden).
inline int fitSize(SizeRole role, const FitItem *items, int count, TextWidthFn measure) {
    int ladderCount;
    const int *ladder = ladderFor(role, &ladderCount);
    for (int i = 0; i < ladderCount; i++) {
        int size = ladder[i];
        bool fits = true;
        for (int j = 0; j < count; j++) {
            if (measure(role, size, items[j].text) > items[j].widthPx) { fits = false; break; }
        }
        if (fits) return size;
    }
    return ladder[ladderCount - 1];
}

struct RoleSizes { int title, stat, chrome; };

// Shrinks exactly one role by one ladder rung, in fixed priority order:
// chrome (least prominent) first, then stat, title last -- so the title
// can never end up smaller than the board line under it just because it
// started from a bigger ladder rung. Returns false once every role is
// already at its ladder's floor (nothing left to shrink).
inline bool shrinkOneStep(RoleSizes &sizes) {
    int chromeCount, statCount, titleCount;
    const int *chromeLadder = ladderFor(SizeRole::Chrome, &chromeCount);
    const int *statLadder = ladderFor(SizeRole::Stat, &statCount);
    const int *titleLadder = ladderFor(SizeRole::Title, &titleCount);

    for (int i = 0; i < chromeCount - 1; i++) {
        if (chromeLadder[i] == sizes.chrome) { sizes.chrome = chromeLadder[i + 1]; return true; }
    }
    for (int i = 0; i < statCount - 1; i++) {
        if (statLadder[i] == sizes.stat) { sizes.stat = statLadder[i + 1]; return true; }
    }
    for (int i = 0; i < titleCount - 1; i++) {
        if (titleLadder[i] == sizes.title) { sizes.title = titleLadder[i + 1]; return true; }
    }
    return false;
}

// ---- QR geometry: matches the firmware's ricmoo/QRCode usage exactly --
// fixed version 4 (33x33 modules), a 4-module quiet zone on each side,
// scaled 1-4px/module. QR_MIN_SCALE is the guaranteed-scannable floor: a
// QR rendered smaller isn't degraded, it's useless (nobody can scan it),
// so "small but present" is worse than "absent, URL shown as text
// instead" -- a starting assumption (see the design spec's open
// questions), not a measured number.
constexpr int QR_MODULES = 33;
constexpr int QR_QUIET_MODULES = 4;
constexpr int QR_TOTAL_MODULES = QR_MODULES + 2 * QR_QUIET_MODULES; // 41
constexpr int QR_MIN_SCALE = 2;

// ---- Grid: everything is grouped into four zones read top to bottom
// (header -> status -> action -> legend). Spacing is a small set of
// named multiples of one grid unit (the chrome size, the smallest text
// on the screen) instead of a different ad-hoc ratio per gap.
constexpr float GRID_OUTER_MARGIN = 0.8f; // top/bottom margin, x chrome size
constexpr float GRID_SECTION_GAP = 1.5f;  // gap between zones, x chrome size
constexpr float GRID_QR_GAP = 0.6f;       // gap above/below the QR, x chrome size

// Which zones this content has (statCount/legendCount == 0 means that
// zone doesn't exist) -- used by computeFit's height model so it can't
// disagree with the renderer about how many section gaps exist.
struct ContentShape {
    int statCount;
    bool hasCaption, hasQr, hasScan, hasUrl;
    int legendCount;
};

struct FitResult {
    float fixedPx;   // everything except the QR
    bool hasQr;
    int qrScale;      // 0 if no QR
    int qrPx;
    float qrGapPx;
    bool qrTight;     // true if qrPx > rowW, or qrScale < QR_MIN_SCALE
    float totalPx;
};

// Stacked height of everything except the QR, plus (if present) the QR
// itself sized to fill whatever vertical room is left -- never scaled
// below 1px/module, same as the real firmware would draw it even when
// that means it doesn't fully fit (flagged via qrTight, not hidden).
inline FitResult computeFit(const ContentShape &shape, const RoleSizes &sizes, int panelH, int rowW) {
    const float unit = (float)sizes.chrome;
    int zoneCount = 1; // header always present
    if (shape.statCount > 0) zoneCount++;
    const bool hasAction = shape.hasCaption || shape.hasQr || shape.hasScan || shape.hasUrl;
    if (hasAction) zoneCount++;
    if (shape.legendCount > 0) zoneCount++;

    float fixed = 2.0f * unit * GRID_OUTER_MARGIN;
    fixed += (zoneCount - 1) * unit * GRID_SECTION_GAP;
    fixed += sizes.title * LINE_HEIGHT + sizes.chrome * LINE_HEIGHT; // header: title + board lines
    fixed += shape.statCount * sizes.stat * LINE_HEIGHT;
    if (shape.hasCaption) fixed += sizes.chrome * LINE_HEIGHT;
    if (shape.hasScan) fixed += sizes.chrome * LINE_HEIGHT;
    if (shape.hasUrl) fixed += sizes.chrome * LINE_HEIGHT;
    fixed += shape.legendCount * sizes.chrome * LINE_HEIGHT;
    fixed *= 1.03f; // headroom for rounding across this many terms

    FitResult r{};
    r.fixedPx = fixed;
    r.hasQr = shape.hasQr;
    if (shape.hasQr) {
        const float qrGap = unit * GRID_QR_GAP;
        const float budget = panelH - fixed - 2 * qrGap;
        int scale = 4;
        // Constrained by height (does the vertical budget fit scale*41
        // tall) AND width (does the panel's own row fit scale*41 wide) --
        // missing the width half of this let a QR pick a scale that
        // towered over a narrow panel's own row.
        while (scale > 1 && (budget / QR_TOTAL_MODULES < scale || QR_TOTAL_MODULES * scale > rowW))
            scale--;
        r.qrScale = scale;
        r.qrPx = QR_TOTAL_MODULES * scale;
        r.qrGapPx = qrGap;
        r.qrTight = (r.qrPx > rowW) || (r.qrScale < QR_MIN_SCALE);
        r.totalPx = fixed + 2 * qrGap + r.qrPx;
    } else {
        r.qrScale = 0; r.qrPx = 0; r.qrGapPx = 0; r.qrTight = false;
        r.totalPx = fixed;
    }
    return r;
}

// ---- Content reduction cascade: ordered least-essential-first. A
// screen starts at level 0 (full content) and steps through these until
// the content fits at a legible size (see status_content.h's
// fitScreen()). Fixed order, cumulative -- level N includes every
// reduction from levels < N too.
enum class LegendMode : uint8_t { Full, Combined, None };
enum class CaptionMode : uint8_t { Full, Short, None };

struct ContentConfig {
    LegendMode legendMode;
    CaptionMode captionMode;
    bool showUrl;
    bool showNextStat;
    bool showVersion;
    bool showScan;
    bool statDetail;
    bool showQr;
};

constexpr int REDUCTION_LEVELS = 10;

inline ContentConfig configForLevel(int level) {
    ContentConfig cfg{};
    cfg.legendMode = LegendMode::Full;
    cfg.captionMode = CaptionMode::Full;
    cfg.showUrl = true;
    cfg.showNextStat = true;
    cfg.showVersion = true;
    cfg.showScan = true;
    cfg.statDetail = true;
    cfg.showQr = true;

    if (level > 0) cfg.legendMode = LegendMode::Combined;
    if (level > 1) cfg.legendMode = LegendMode::None;
    if (level > 2) cfg.statDetail = false;
    if (level > 3) cfg.captionMode = CaptionMode::Short;
    if (level > 4) cfg.showUrl = false;
    if (level > 5) cfg.showNextStat = false;
    if (level > 6) cfg.showVersion = false;
    if (level > 7) cfg.captionMode = CaptionMode::None;
    if (level > 8) cfg.showScan = false;
    if (level > 9) cfg.showQr = false;
    return cfg;
}
```

- [ ] **Step 4: Delete the now-obsolete `currentLayout()` wrapper**

`src/layout.h`/`src/layout.cpp` exist only to call the old `computeLayout()`/`LayoutMetrics` this step just removed — PlatformIO auto-discovers and compiles every file under `src/**`, so leaving them in place would fail the board build with "no such function" errors rather than cleanly not-compiling. Delete both:

```bash
git rm src/layout.h src/layout.cpp
```

Their only callers are `src/ui.cpp` and `src/net.cpp`, both still on the *old* `LayoutMetrics`-based implementation at this point in the plan — **this means `pio run -e ee02` (and ee03/ee04/ee05) is expected to fail from this step until Task 5 lands**, which rewrites both callers to use the new engine instead. Native tests are unaffected (the `native` env never compiles `src/ui.cpp`/`src/net.cpp`/`src/layout.cpp`). This is a deliberate, disclosed intermediate state, not a mistake to fix mid-task — Tasks 3 and 4 add new, independent files without touching `ui.cpp`/`net.cpp`, so there's no earlier point where re-fixing the board build is possible without doing Task 5's work early.

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_layout_math`
Expected: PASS (11 tests) — this only exercises the `native` env, which is unaffected by the board-build breakage above.

- [ ] **Step 6: Commit**

```bash
git add src/logic/layout_math.h test/test_layout_math/main.cpp
git commit -m "layout_math: replace fixed two-tier layout with font-ladder sizing engine"
```

---

### Task 3: Content-line building (`src/logic/status_content.h`, new)

**Files:**
- Create: `src/logic/status_content.h`
- Test: `test/test_status_content/main.cpp`

**Interfaces:**
- Consumes: everything Task 2 produces (`SizeRole`, `TextWidthFn`, `FitItem`, `fitSize`, `RoleSizes`, `shrinkOneStep`, `ContentShape`, `FitResult`, `computeFit`, `ContentConfig`, `configForLevel`, `REDUCTION_LEVELS`, `QR_MIN_SCALE`, `ICON_COL_RESERVE_FACTOR`).
- Produces: `ScreenState` enum (`Normal`/`Onboarding`/`Error`), `LineKind` enum (`Title`/`Board`/`Stat`/`Caption`/`Scan`/`Url`/`Legend`), `StatIcon` enum (`None`/`Battery`/`Wifi`/`Refresh`), `MAX_CONTENT_LINES` (`12`), `ContentLine{LineKind kind; SizeRole sizeRole; StatIcon icon; int batteryPct; char keyLabel[4]; char text[64];}`, `ScreenContent{int batteryPct; char batteryVoltage[16]; char wifiBase[16]; char wifiSsid[32]; char nextBase[16]; char lastFetch[32]; char settingsUrl[48]; char errorMsg[64];}`, `buildLines(ScreenState, const ContentConfig&, const ScreenContent&, const char *boardModel, const char *versionHash, const char *apName, ContentLine *out, int maxOut) -> int`, `FitLine{ContentLine line; int fontPx; int widthPx;}`, `ScreenFit{FitLine lines[MAX_CONTENT_LINES]; int lineCount; RoleSizes sizes; int marginX; int rowW; int qrScale; int qrPx; float qrGapPx; int level;}`, `fitScreen(ScreenState, const ScreenContent&, const char *boardModel, const char *versionHash, const char *apName, int panelW, int panelH, TextWidthFn measure) -> ScreenFit`. Consumed by Task 5 (`ui.cpp`'s `drawFrameScreen()`/`gatherLiveContent()` and `net.cpp`'s `showProvisioningScreen()`, both in that task) and Task 6 (`display.cpp`'s `showError()`).

- [ ] **Step 1: Write the failing test**

Create `test/test_status_content/main.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_status_content`
Expected: FAIL to compile — `status_content.h` does not exist yet.

- [ ] **Step 3: Create `src/logic/status_content.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "layout_math.h"

// Content model and top-level fitting orchestration for the unified
// status/onboarding/error screen. Pure logic: host-testable, no Arduino
// deps. See docs/superpowers/specs/2026-08-18-unified-status-screen-design.md.

enum class ScreenState : uint8_t { Normal, Onboarding, Error };
enum class LineKind : uint8_t { Title, Board, Stat, Caption, Scan, Url, Legend };
enum class StatIcon : uint8_t { None, Battery, Wifi, Refresh };

constexpr int MAX_CONTENT_LINES = 12; // header(2) + stat(<=3) + caption(<=1)
                                       // + scan(<=1) + url(<=1) + legend(<=3)

struct ContentLine {
    LineKind kind;
    SizeRole sizeRole;   // which ladder/measurement role this line uses
    StatIcon icon;       // Stat lines only; StatIcon::None otherwise
    int batteryPct;      // meaningful only when icon == StatIcon::Battery
    char keyLabel[4];    // Legend lines only: "1"/"2"/"3", "" for combined
    char text[64];
};

// Everything a caller (ui.cpp/net.cpp/display.cpp) supplies per draw --
// board model and version are compile-time constants passed separately
// to buildLines()/fitScreen(), not carried here.
struct ScreenContent {
    int batteryPct;
    char batteryVoltage[16]; // "4.17V"; "" = no detail (onboarding)
    char wifiBase[16];       // "strong"/"fair"/"weak"/"--"/"failed"
    char wifiSsid[32];       // ""  = no detail
    char nextBase[16];       // "16:32"/"pinned"/"--"
    char lastFetch[32];      // "last Wed 14:32"; "" = no detail
    char settingsUrl[48];    // portalUrl(), e.g. "http://ee02.local"
    char errorMsg[64];       // Error state's instruction-line text; "" elsewhere
};

inline void setLine(ContentLine &l, LineKind kind, SizeRole role, const char *text) {
    l.kind = kind;
    l.sizeRole = role;
    l.icon = StatIcon::None;
    l.batteryPct = 0;
    l.keyLabel[0] = '\0';
    strncpy(l.text, text, sizeof(l.text) - 1);
    l.text[sizeof(l.text) - 1] = '\0';
}

// Builds this state's content lines under `cfg`'s reduction settings.
// Returns how many were written into `out` (capacity `maxOut`).
inline int buildLines(ScreenState state, const ContentConfig &cfg,
                      const ScreenContent &content, const char *boardModel,
                      const char *versionHash, const char *apName,
                      ContentLine *out, int maxOut) {
    int n = 0;

    // ---- Header: title (+version), board model -- two lines, always
    // present, each running the full row width on its own.
    if (n < maxOut) {
        char buf[64];
        if (cfg.showVersion) snprintf(buf, sizeof(buf), "Hello ePaper (ver.%s)", versionHash);
        else snprintf(buf, sizeof(buf), "Hello ePaper");
        setLine(out[n], LineKind::Title, SizeRole::Title, buf);
        n++;
    }
    if (n < maxOut) {
        char buf[32];
        snprintf(buf, sizeof(buf), "XIAO %s", boardModel);
        setLine(out[n], LineKind::Board, SizeRole::Chrome, buf);
        n++;
    }

    // ---- Stats: battery, Wi-Fi, next -- one line each, icon carries the
    // word label ("battery"/"Wi-Fi"/"next" is dropped from the text).
    if (n < maxOut) {
        char buf[32];
        if (cfg.statDetail && content.batteryVoltage[0])
            snprintf(buf, sizeof(buf), "%d%% \xC2\xB7 %s", content.batteryPct, content.batteryVoltage);
        else
            snprintf(buf, sizeof(buf), "%d%%", content.batteryPct);
        setLine(out[n], LineKind::Stat, SizeRole::Stat, buf);
        out[n].icon = StatIcon::Battery;
        out[n].batteryPct = content.batteryPct;
        n++;
    }
    if (n < maxOut) {
        char buf[48];
        if (cfg.statDetail && content.wifiSsid[0])
            snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", content.wifiBase, content.wifiSsid);
        else
            snprintf(buf, sizeof(buf), "%s", content.wifiBase);
        setLine(out[n], LineKind::Stat, SizeRole::Stat, buf);
        out[n].icon = StatIcon::Wifi;
        n++;
    }
    if (cfg.showNextStat && n < maxOut) {
        char buf[48];
        if (cfg.statDetail && content.lastFetch[0])
            snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", content.nextBase, content.lastFetch);
        else
            snprintf(buf, sizeof(buf), "%s", content.nextBase);
        setLine(out[n], LineKind::Stat, SizeRole::Stat, buf);
        out[n].icon = StatIcon::Refresh;
        n++;
    }

    // ---- Action zone: caption (onboarding only), QR (drawn separately
    // by the caller -- not a ContentLine), scan instruction, URL/AP text.
    const char *captionFull = nullptr, *captionShort = nullptr;
    char capFullBuf[48], capShortBuf[32];
    const char *scanText = nullptr;
    const char *urlText = nullptr;

    if (state == ScreenState::Onboarding) {
        snprintf(capFullBuf, sizeof(capFullBuf), "Join \"%s\" to set up Wi-Fi", apName);
        snprintf(capShortBuf, sizeof(capShortBuf), "Join \"%s\"", apName);
        captionFull = capFullBuf; captionShort = capShortBuf;
        scanText = "Scan, or connect manually";
        urlText = apName; // shown as text next to the QR (open Wi-Fi)
    } else if (state == ScreenState::Error) {
        scanText = content.errorMsg[0] ? content.errorMsg : "Wi-Fi failed";
        urlText = content.settingsUrl;
    } else {
        scanText = "Scan to open settings";
        urlText = content.settingsUrl;
    }

    if (n < maxOut) {
        if (cfg.captionMode == CaptionMode::Full && captionFull)
            { setLine(out[n], LineKind::Caption, SizeRole::Chrome, captionFull); n++; }
        else if (cfg.captionMode == CaptionMode::Short && captionShort)
            { setLine(out[n], LineKind::Caption, SizeRole::Chrome, captionShort); n++; }
    }
    if (cfg.showScan && scanText && n < maxOut)
        { setLine(out[n], LineKind::Scan, SizeRole::Chrome, scanText); n++; }
    if (cfg.showUrl && urlText && n < maxOut)
        { setLine(out[n], LineKind::Url, SizeRole::Chrome, urlText); n++; }

    // ---- Legend: KEY1/KEY2/KEY3 rows (full), one combined line, or none.
    const char *legendPhrase[3];
    char combinedBuf[64];
    if (state == ScreenState::Onboarding) {
        legendPhrase[0] = "Show Screen"; legendPhrase[1] = "--"; legendPhrase[2] = "--";
        snprintf(combinedBuf, sizeof(combinedBuf), "KEY1 Show Screen");
    } else if (state == ScreenState::Error) {
        legendPhrase[0] = "Configuration"; legendPhrase[1] = "Retry"; legendPhrase[2] = "Pin";
        snprintf(combinedBuf, sizeof(combinedBuf), "KEY1 Config \xC2\xB7 KEY2 Retry \xC2\xB7 KEY3 Pin");
    } else {
        legendPhrase[0] = "Configuration"; legendPhrase[1] = "Refresh"; legendPhrase[2] = "Pin";
        snprintf(combinedBuf, sizeof(combinedBuf), "KEY1 Config \xC2\xB7 KEY2 Refresh \xC2\xB7 KEY3 Pin");
    }
    if (cfg.legendMode == LegendMode::Full) {
        for (int k = 0; k < 3 && n < maxOut; k++) {
            setLine(out[n], LineKind::Legend, SizeRole::Chrome, legendPhrase[k]);
            out[n].keyLabel[0] = char('1' + k);
            out[n].keyLabel[1] = '\0';
            n++;
        }
    } else if (cfg.legendMode == LegendMode::Combined && n < maxOut) {
        // keyLabel left empty -> rendered full-width, no icon column (see
        // the design spec's firmware-deviation note in the plan header).
        setLine(out[n], LineKind::Legend, SizeRole::Chrome, combinedBuf);
        n++;
    }

    return n;
}

struct FitLine {
    ContentLine line;
    int fontPx;
    int widthPx; // available width this line was fitted against
};

struct ScreenFit {
    FitLine lines[MAX_CONTENT_LINES];
    int lineCount;
    RoleSizes sizes;
    int marginX, rowW;
    int qrScale; // 0 if no QR
    int qrPx;
    float qrGapPx;
    int level; // reduction level actually applied
};

inline bool hasIconColumn(const ContentLine &l) {
    return l.kind == LineKind::Stat || (l.kind == LineKind::Legend && l.keyLabel[0] != '\0');
}

// The fitting algorithm: at each reduction level, build this state's
// lines, fit title/stat/chrome each to the largest real size that fits
// every line sharing that role (with a two-pass icon-column reserve for
// stat/legend), then run the height-aware shrink pass. If the result
// still doesn't fit (or any single line is measured tighter than its
// available width, or the QR is below its scannable floor), advance to
// the next reduction level and try again -- up to REDUCTION_LEVELS,
// which always terminates.
inline ScreenFit fitScreen(ScreenState state, const ScreenContent &content,
                           const char *boardModel, const char *versionHash,
                           const char *apName, int panelW, int panelH,
                           TextWidthFn measure) {
    const int marginX = panelW / 12;
    const int rowW = panelW - 2 * marginX;

    ContentLine lines[MAX_CONTENT_LINES];
    int lineCount = 0;
    RoleSizes sizes{};
    int level = 0;

    for (level = 0; level <= REDUCTION_LEVELS; level++) {
        ContentConfig cfg = configForLevel(level);
        lineCount = buildLines(state, cfg, content, boardModel, versionHash,
                               apName, lines, MAX_CONTENT_LINES);

        // ---- computeSizes: two-pass icon-column reserve for stat lines
        // (a rough full-width fit just to size the column, then the real
        // fit against the reduced width -- the column's own width comes
        // from the stat size, which is exactly what's being solved for).
        FitItem statItemsRough[3];
        int statCount = 0;
        for (int i = 0; i < lineCount; i++)
            if (lines[i].kind == LineKind::Stat)
                statItemsRough[statCount++] = FitItem{lines[i].text, rowW};
        int statSize = statCount
            ? fitSize(SizeRole::Stat, statItemsRough, statCount, measure)
            : STAT_SIZES[STAT_SIZES_N - 1];
        float reserve = statSize * ICON_COL_RESERVE_FACTOR;

        FitItem statItems[3];
        for (int i = 0; i < statCount; i++) {
            int w = (int)(rowW - reserve);
            statItems[i] = FitItem{statItemsRough[i].text, w > 10 ? w : 10};
        }
        statSize = statCount
            ? fitSize(SizeRole::Stat, statItems, statCount, measure)
            : STAT_SIZES[STAT_SIZES_N - 1];
        reserve = statSize * ICON_COL_RESERVE_FACTOR; // refine with the real (not rough) size

        FitItem titleItems[1];
        int titleCount = 0;
        for (int i = 0; i < lineCount; i++)
            if (lines[i].kind == LineKind::Title)
                titleItems[titleCount++] = FitItem{lines[i].text, rowW};
        int titleSize = titleCount
            ? fitSize(SizeRole::Title, titleItems, titleCount, measure)
            : TITLE_SIZES[TITLE_SIZES_N - 1];

        FitItem chromeItems[9]; // board + caption + scan + url + up to 3 legend (or 1 combined)
        int chromeCount = 0;
        for (int i = 0; i < lineCount && chromeCount < 9; i++) {
            if (lines[i].sizeRole != SizeRole::Chrome) continue;
            int w = hasIconColumn(lines[i]) ? (int)(rowW - reserve) : rowW;
            chromeItems[chromeCount++] = FitItem{lines[i].text, w > 10 ? w : 10};
        }
        int chromeSize = chromeCount
            ? fitSize(SizeRole::Chrome, chromeItems, chromeCount, measure)
            : CHROME_SIZES[CHROME_SIZES_N - 1];

        sizes = RoleSizes{titleSize, statSize, chromeSize};

        // ---- Shape + height-aware shrink pass.
        ContentShape shape{};
        shape.statCount = statCount;
        for (int i = 0; i < lineCount; i++) {
            if (lines[i].kind == LineKind::Caption) shape.hasCaption = true;
            if (lines[i].kind == LineKind::Scan) shape.hasScan = true;
            if (lines[i].kind == LineKind::Url) shape.hasUrl = true;
            if (lines[i].kind == LineKind::Legend) shape.legendCount++;
        }
        shape.hasQr = cfg.showQr;

        FitResult fit = computeFit(shape, sizes, panelH, rowW);
        while ((fit.totalPx > panelH || fit.qrTight) && shrinkOneStep(sizes)) {
            fit = computeFit(shape, sizes, panelH, rowW);
        }
        // Shrinking may have changed sizes.stat since the icon-column
        // reserve above was computed -- recompute once more so the final
        // per-line width check reflects the column actually rendered.
        reserve = sizes.stat * ICON_COL_RESERVE_FACTOR;

        bool anyTight = fit.qrTight;
        FitLine outLines[MAX_CONTENT_LINES];
        for (int i = 0; i < lineCount; i++) {
            int fontPx = lines[i].sizeRole == SizeRole::Title ? sizes.title
                       : lines[i].sizeRole == SizeRole::Stat  ? sizes.stat
                                                               : sizes.chrome;
            int w = hasIconColumn(lines[i]) ? (int)(rowW - reserve) : rowW;
            int widthPx = w > 10 ? w : 10;
            outLines[i].line = lines[i];
            outLines[i].fontPx = fontPx;
            outLines[i].widthPx = widthPx;
            if (measure(lines[i].sizeRole, fontPx, lines[i].text) > widthPx) anyTight = true;
        }

        if ((fit.totalPx <= panelH && !anyTight) || level == REDUCTION_LEVELS) {
            ScreenFit result{};
            for (int i = 0; i < lineCount; i++) result.lines[i] = outLines[i];
            result.lineCount = lineCount;
            result.sizes = sizes;
            result.marginX = marginX;
            result.rowW = rowW;
            result.qrScale = fit.hasQr ? fit.qrScale : 0;
            result.qrPx = fit.qrPx;
            result.qrGapPx = fit.qrGapPx;
            result.level = level;
            return result;
        }
    }

    ScreenFit fallback{}; // unreachable: the loop above always returns by
                          // level == REDUCTION_LEVELS
    return fallback;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_status_content`
Expected: PASS (8 tests)

- [ ] **Step 5: Commit**

```bash
git add src/logic/status_content.h test/test_status_content/main.cpp
git commit -m "status_content: pure-logic content-line building + fitScreen orchestration"
```

---

### Task 4: Bitmap icon rendering + `PortraitScope` (`src/display.h`/`src/display.cpp`)

**Files:**
- Modify: `src/display.h`
- Modify: `src/display.cpp`

**Interfaces:**
- Consumes: `ICON_W`/`ICON_H` (Task 1), `PANEL_NATIVE_LANDSCAPE`/`applyOrientation()` (already in `display.h`).
- Produces: `struct PortraitScope` (ctor forces portrait, dtor restores via `applyOrientation()`), `drawStatusIcon(const uint8_t *bitmap, int x, int y, uint32_t fgColor)`, `drawKeycap(const char *digit, int cx, int cy, int sizePx, uint32_t fgColor)`. Removes `drawBatteryIcon()`/`drawWifiIcon()`/`drawNextPhotoIcon()`/`chargingBoltColor()` (dead code once Task 5 replaces their only caller; `chargingBoltColor()` has no replacement -- see this plan's Global Constraints note on the dropped charging indicator). Consumed by Task 5 (`ui.cpp`, `net.cpp`) and Task 6 (`display.cpp`'s own `showError()`).
- No native test: this task is Arduino/`epaper`-dependent rendering code, verified by the board-env build check in Task 5's Step 7 (first green build) and Task 7's final verification (per this plan's Global Constraints on EE02-only hardware verification).

- [ ] **Step 1: Replace `src/display.h` in full**

```cpp
#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo
#include "icons.h"
#include "logic/battery_curve.h"
#include "logic/wifi_strength.h"

extern EPaper epaper;

// True RGB565 color at a sprite pixel, correctly reversing this
// project's palette-nibble/truthiness storage scheme (see ditherToPanel
// in display.cpp): drawPixel() there stores a masked/truthiness-tested
// PALETTE index directly, bypassing color matching entirely, so
// EPaper::readPixel() -- which decodes through TFT_eSPI's generic (and,
// for this storage scheme, unrelated and never-populated-by-us)
// _colorMap -- returns nonsense. Use this instead wherever a pixel needs
// to be read back as its true displayed color (e.g. screencapture.cpp).
uint16_t truePixelColor(int x, int y);

// True if this panel's native (rotation 0) shape is wider than tall.
// EE02's native panel is portrait (1200x1600), but EE03/EE04/EE05's native
// panels are landscape (e.g. 800x480) -- rotation 0 does NOT universally
// mean "portrait", so the rotation dropdown's labels must be computed from
// this per board, not hardcoded (see portal.cpp's rotOptions()), and the
// unified status screen's portrait-forcing (PortraitScope below) uses it
// to pick the right rotation value too.
constexpr bool PANEL_NATIVE_LANDSCAPE = TFT_WIDTH > TFT_HEIGHT;

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// RAII scope guard for the unified status/onboarding/error screen (see
// docs/superpowers/specs/2026-08-18-unified-status-screen-design.md):
// forces portrait regardless of settings.rotation for its lifetime, then
// restores the configured orientation on scope exit. Construct one
// around the draw + epaper.update() call for that screen -- restoring
// only after update() matters, since the sprite's own dimensions (and
// therefore what update() pushes) follow whatever rotation is active
// while it's drawn.
struct PortraitScope {
    PortraitScope();
    ~PortraitScope();
};

// Version-4 (33x33) QR centered at (cx, cy), scale px per module, with a
// 4-module white quiet zone. Draws into the sprite only. Payload must fit
// version 4 at ECC_LOW (78 bytes).
void drawQrCode(const String &text, int cx, int cy, int scale);

// Draws a status-screen icon (see icons.h, always ICON_W x ICON_H) with
// its top-left corner at (x, y). One fixed size regardless of the
// surrounding text's font tier -- callers position by the sizing
// engine's icon-column width, not by icon size.
void drawStatusIcon(const uint8_t *bitmap, int x, int y, uint32_t fgColor);

// Legend zone's small square keycap glyph ("1"/"2"/"3") -- a drawn
// rounded box + centered digit (classic Font2), not a bitmap: the digit
// itself is text, unlike battery/Wi-Fi/refresh which have no text
// equivalent.
void drawKeycap(const char *digit, int cx, int cy, int sizePx, uint32_t fgColor);

// Board-appropriate battery fill color: functional green/yellow/red on
// 6-color Spectra panels (EE02), solid black everywhere else -- the
// percentage number and bar fill length already carry the information on
// grayscale/mono panels, so no data is lost by dropping the color cue.
uint32_t batteryColorForLevel(BatteryLevel level);

// Decode a baseline JPEG into PSRAM, Floyd-Steinberg dither it to the
// panel's palette, and write it into the sprite (no update()).
bool renderJpeg(uint8_t *buf, size_t len);

// For gray-capable panels (e.g. EE03): switch the sprite into
// USE_MUTIGRAY_EPAPER's gray mode. No-op on panels that don't support it.
// Call once after epaper.begin() / applyOrientation(), before drawing.
void initPanelColorMode();

// Full-panel error screen. Gathers live battery/Wi-Fi/next-fetch state
// (see ui.h's gatherLiveContent()) plus `msg`, then draws the unified
// frame's Error state and calls update(). Only drawn when someone is
// watching (button-initiated actions) -- unattended wakes keep the photo.
void showError(const String &msg);
```

- [ ] **Step 2: Edit `src/display.cpp`** -- add `PortraitScope`, `drawStatusIcon()`, `drawKeycap()` after `applyOrientation()`, and remove the three vector-icon functions plus `chargingBoltColor()`

Replace:
```cpp
void applyOrientation() { epaper.setRotation(settings.rotation); }
```
with:
```cpp
void applyOrientation() { epaper.setRotation(settings.rotation); }

PortraitScope::PortraitScope() {
    // Rotation 0 is landscape-shaped on a native-landscape panel, so
    // rotation 1 (90 deg) is the portrait one there; on a native-portrait
    // panel rotation 0 already is portrait.
    epaper.setRotation(PANEL_NATIVE_LANDSCAPE ? 1 : 0);
}
PortraitScope::~PortraitScope() { applyOrientation(); }

void drawStatusIcon(const uint8_t *bitmap, int x, int y, uint32_t fgColor) {
    epaper.drawBitmap(x, y, bitmap, ICON_W, ICON_H, fgColor);
}

void drawKeycap(const char *digit, int cx, int cy, int sizePx, uint32_t fgColor) {
    const int half = sizePx / 2;
    epaper.drawRoundRect(cx - half, cy - half, sizePx, sizePx, sizePx / 6, fgColor);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextColor(fgColor, TFT_WHITE);
    epaper.setTextSize(1);
    epaper.drawString(digit, cx, cy, 2); // classic Font2, legible at any legend size
}
```

Remove the `chargingBoltColor()` function:
```cpp
uint32_t chargingBoltColor() {
#if defined(USE_COLORFULL_EPAPER)
    return TFT_BLUE;
#else
    return TFT_BLACK;
#endif
}
```

Remove the three vector-icon functions (`drawBatteryIcon()`, `drawWifiIcon()`, `drawNextPhotoIcon()`, including their doc comments) -- from the `// Classic battery glyph...` comment through the end of `drawNextPhotoIcon()`'s closing brace.

- [ ] **Step 3: Confirm the expected failure is unchanged in kind**

`pio run -e ee02` is still expected to fail here (same reason as Task 2 Step 4: `src/ui.cpp` isn't rewritten until Task 5) — this task additionally removes `drawBatteryIcon()`/`drawWifiIcon()`/`drawNextPhotoIcon()`, which `ui.cpp`'s still-old implementation calls, so the failure now also names those symbols. Run it and confirm the errors are confined to `ui.cpp` (referencing `LayoutMetrics`, `currentLayout`, and the three removed icon functions) — not `display.cpp`/`display.h` themselves, and not `net.cpp`:

```bash
pio run -e ee02 2>&1 | grep -E "error:" | grep -v "ui.cpp"
```
Expected: no output (every remaining error is in `ui.cpp`, fixed in Task 5). `showError()` itself is untouched in this task and keeps compiling against its old, unchanged implementation — it's rewritten in Task 6, as a non-breaking swap, once `ui.h` exists.

- [ ] **Step 4: Commit**

```bash
git add src/display.h src/display.cpp
git commit -m "display: bitmap icon/keycap drawing + PortraitScope, remove vector icons"
```

---

### Task 5: `drawFrameScreen()` + status/onboarding wrappers (`src/ui.h`/`src/ui.cpp`, `src/main.cpp`, `src/net.cpp`)

This task lands `ui.cpp` and `net.cpp` together, not as two separate tasks: both are broken from Task 2 onward (they call the removed `LayoutMetrics`/`currentLayout()`), and the board build can only turn green again once *both* are fixed — there's no meaningful intermediate state where one alone compiles. `display.cpp`'s `showError()` is different (it never referenced the removed API) and stays its own task (Task 6) since it doesn't share this constraint.

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Modify: `src/main.cpp:78-86` (`runStatusMode()`)
- Modify: `src/net.cpp:1-100` (`showProvisioningScreen()` + its includes)

**Interfaces:**
- Consumes: `ScreenState`/`ScreenContent`/`ScreenFit`/`FitLine`/`LineKind`/`StatIcon`/`fitScreen` (Task 3), `SizeRole`/`LINE_HEIGHT` (Task 2), `ICON_W`/`ICON_H`/icon arrays (Task 1), `PortraitScope`/`drawStatusIcon`/`drawKeycap`/`drawQrCode`/`batteryColorForLevel` (Task 4).
- Produces: `drawFrameScreen(ScreenState, const ScreenContent&)` (declared in `ui.h`, defined in `ui.cpp` -- draws into the sprite only, no `epaper.update()`), `gatherLiveContent(int32_t vbatMv, int32_t deltaMv, bool haveDelta) -> ScreenContent` (declared in `ui.h`). `drawStatusScreen(int32_t, int32_t, bool)` keeps its existing signature but changes contract: it now calls `epaper.update()` itself (previously the caller did) -- `main.cpp`'s `runStatusMode()` is updated in this same task to match. Consumed by Task 6 (`display.cpp`).

- [ ] **Step 1: Replace `src/ui.h` in full**

```cpp
#pragma once
#include <Arduino.h>
#include "logic/status_content.h"

// "timer" (hourly refresh), "btn-info"/"btn-new-pic"/"btn-pin" (which
// function's button ended the sleep), or "power-on" (cold start: power
// switch, USB plug, RESET, or a fresh flash).
const char *wakeReason();

// Persist what the info page needs on non-fetch wakes. Must run on EVERY
// successful fetch — otherwise the page shows stale data for a photo it
// doesn't describe.
void recordFetchMetadata();

// Draws `state`'s unified frame (header/status/action/legend, see the
// design spec) into the sprite only -- caller wraps the call in a
// PortraitScope and calls epaper.update() afterward. Shared by all three
// screen call sites (drawStatusScreen/showProvisioningScreen/showError),
// each a thin wrapper: gather its own ScreenContent, call this.
void drawFrameScreen(ScreenState state, const ScreenContent &content);

// Battery/Wi-Fi/next-fetch fields shared by the status and error screens
// (onboarding has no Wi-Fi/next-fetch data yet -- net.cpp fills its own
// ScreenContent directly). vbatMv/deltaMv/haveDelta: the caller's own
// battery read.
ScreenContent gatherLiveContent(int32_t vbatMv, int32_t deltaMv, bool haveDelta);

// Full-screen status page: wake/battery/wifi/refresh info, the settings
// portal URL + QR code, and a button legend with live state. Draws,
// forces portrait for the duration, and calls epaper.update() itself.
void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta);
```

- [ ] **Step 2: Replace `src/ui.cpp` in full**

```cpp
#include "ui.h"
#include "config.h"
#include "display.h"
#include "icons.h"
#include "screencapture.h"
#include "layout.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "logic/battery_curve.h"
#include "logic/wifi_strength.h"
#include <WiFi.h>
#include <time.h>
// FreeSansBold24pt7b/18pt7b/12pt7b/9pt7b and FreeSans18pt7b/12pt7b/9pt7b
// are already pulled in unconditionally by Seeed_GFX's gfxfont.h (via
// <TFT_eSPI.h>, included through display.h) whenever LOAD_GFXFF is
// defined -- these headers have no include guards, so including them
// again here would double-define their font data.

const char *wakeReason() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t bits = esp_sleep_get_ext1_wakeup_status();
            if (bits & (1ULL << BTN_INFO))    return "btn-info";
            if (bits & (1ULL << BTN_NEW_PIC)) return "btn-new-pic";
            if (bits & (1ULL << BTN_PIN))     return "btn-pin";
            return "button";
        }
        default: return "power-on";
    }
}

void recordFetchMetadata() {
    // Don't stamp a never-synced clock as the fetch time — keep the
    // previous (accurate) epoch instead and let the page show that.
    time_t now = time(nullptr);
    if (now > CLOCK_SANE_EPOCH)
        prefs.putULong("lastEpoch", (uint32_t)now);
    // Stored separately (not one pre-formatted string) so gatherLiveContent
    // can read only what it needs.
    prefs.putString("wifiSsid", WiFi.SSID());
    prefs.putInt("wifiRssi", WiFi.RSSI());
}

// "next photo" stat value ("HH:MM", "pinned", or "--").
static String nextPhotoValue() {
    if (held) return "pinned";
    if (time(nullptr) <= CLOCK_SANE_EPOCH) return "--";
    time_t nextT = time(nullptr) + (time_t)plannedSleepSecs();
    struct tm t;
    localtime_r(&nextT, &t);
    char hm[8];
    strftime(hm, sizeof(hm), "%H:%M", &t);
    return String(hm);
}

ScreenContent gatherLiveContent(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    (void)deltaMv; (void)haveDelta; // charging indication has no equivalent
                                     // in the unified design -- see the
                                     // design spec's dropped-feature note
    ScreenContent content{};
    content.batteryPct = batteryPercent(vbatMv);
    snprintf(content.batteryVoltage, sizeof(content.batteryVoltage), "%.2fV", vbatMv / 1000.0f);

    const int rssi = prefs.getInt("wifiRssi", -100);
    const WifiStrength wifiLevel = wifiStrengthBucket(rssi);
    strncpy(content.wifiBase, wifiStrengthLabel(wifiLevel), sizeof(content.wifiBase) - 1);
    String ssid = prefs.getString("wifiSsid", "?");
    strncpy(content.wifiSsid, ssid.c_str(), sizeof(content.wifiSsid) - 1);

    String nextVal = nextPhotoValue();
    strncpy(content.nextBase, nextVal.c_str(), sizeof(content.nextBase) - 1);

    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm t;
        localtime_r(&lastEpoch, &t);
        char buf[24];
        strftime(buf, sizeof(buf), "last %a %H:%M", &t);
        strncpy(content.lastFetch, buf, sizeof(content.lastFetch) - 1);
    }

    String url = portalUrl();
    strncpy(content.settingsUrl, url.c_str(), sizeof(content.settingsUrl) - 1);
    return content;
}

// ---- Font selection: maps a fitted ladder rung to the real compiled-in
// font asset for that role. gfxFont != nullptr means a GFXFF face;
// otherwise classicFont (1 = GLCD, 2 = Font2) selects a classic bitmap
// font.
struct FontChoice { const GFXfont *gfxFont; uint8_t classicFont; };

static FontChoice fontFor(SizeRole role, int sizePx) {
    if (role == SizeRole::Title) {
        switch (sizePx) {
            case 56: return {&FreeSansBold24pt7b, 0};
            case 42: return {&FreeSansBold18pt7b, 0};
            case 29: return {&FreeSansBold12pt7b, 0};
            case 22: return {&FreeSansBold9pt7b, 0};
            case 16: return {nullptr, 2};
            default: return {nullptr, 1}; // 8px
        }
    }
    if (role == SizeRole::Stat) {
        switch (sizePx) {
            case 42: return {&FreeSans18pt7b, 0};
            case 29: return {&FreeSans12pt7b, 0};
            case 22: return {&FreeSans9pt7b, 0};
            case 16: return {nullptr, 2};
            default: return {nullptr, 1};
        }
    }
    switch (sizePx) { // Chrome
        case 29: return {&FreeSans12pt7b, 0};
        case 16: return {nullptr, 2};
        default: return {nullptr, 1};
    }
}

// Applies `f` to `epaper` so a following 0-arg textWidth()/drawString()
// call uses it. Classic Font1 (GLCD) is ambiguous in this library --
// drawing with font number 1 silently reuses whatever GFXFF font is
// still loaded if one is (TFT_eSPI's textWidth()/drawString() treat
// font==1 as "use gfxFont if set, else GLCD") -- so the GLCD case must
// explicitly clear it via setFreeFont(nullptr) first. Font 2 is
// unambiguous (font>1 always uses the classic width table) and needs no
// such precaution.
static void applyFont(const FontChoice &f) {
    if (f.gfxFont) { epaper.setFreeFont(f.gfxFont); return; }
    if (f.classicFont == 1) { epaper.setFreeFont(nullptr); return; } // clears gfxFont, textfont=1
    epaper.setTextFont(f.classicFont); // 2: unambiguous
}

static int realTextWidth(SizeRole role, int sizePx, const char *text) {
    applyFont(fontFor(role, sizePx));
    return epaper.textWidth(text);
}

static void drawFittedText(const char *text, int x, int y, uint8_t datum, SizeRole role, int sizePx) {
    epaper.setTextDatum(datum);
    epaper.setTextSize(1);
    applyFont(fontFor(role, sizePx));
    epaper.drawString(text, x, y);
}

void drawFrameScreen(ScreenState state, const ScreenContent &content) {
    ScreenFit fit = fitScreen(state, content, BOARD_MODEL, FW_GIT_HASH, AP_NAME,
                              epaper.width(), epaper.height(), realTextWidth);

    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);

    const int cx = epaper.width() / 2;
    const int unit = fit.sizes.chrome;
    const int outerMargin = (int)(unit * GRID_OUTER_MARGIN);
    const int sectionGap = (int)(unit * GRID_SECTION_GAP);

    int y = outerMargin;
    bool drewAnyZone = false;

    // ---- Header zone: title, board -- always present, centered.
    for (int i = 0; i < fit.lineCount; i++) {
        FitLine &fl = fit.lines[i];
        if (fl.line.kind != LineKind::Title && fl.line.kind != LineKind::Board) continue;
        int lineH = (int)(fl.fontPx * LINE_HEIGHT);
        y += lineH / 2;
        drawFittedText(fl.line.text, cx, y, MC_DATUM, fl.line.sizeRole, fl.fontPx);
        y += lineH - lineH / 2;
    }
    drewAnyZone = true;

    // ---- Stats zone: icon + text, left-aligned after the icon column.
    bool hasStats = false;
    for (int i = 0; i < fit.lineCount; i++)
        if (fit.lines[i].line.kind == LineKind::Stat) hasStats = true;
    if (hasStats) {
        if (drewAnyZone) y += sectionGap;
        for (int i = 0; i < fit.lineCount; i++) {
            FitLine &fl = fit.lines[i];
            if (fl.line.kind != LineKind::Stat) continue;
            int lineH = (int)(fl.fontPx * LINE_HEIGHT);
            y += lineH / 2;
            const int colCenterX = fit.marginX + (fit.rowW - fl.widthPx) / 2;
            const uint8_t *bmp =
                fl.line.icon == StatIcon::Wifi ? ICON_WIFI :
                fl.line.icon == StatIcon::Refresh ? ICON_REFRESH :
                fl.line.icon == StatIcon::Battery
                    ? (fl.line.batteryPct >= 70 ? ICON_BATTERY_FULL
                     : fl.line.batteryPct >= 30 ? ICON_BATTERY_MEDIUM
                                                 : ICON_BATTERY_LOW)
                    : nullptr;
            uint32_t fg = TFT_BLACK;
            if (fl.line.icon == StatIcon::Battery)
                fg = batteryColorForLevel(batteryLevelBucket(fl.line.batteryPct));
            if (bmp) drawStatusIcon(bmp, colCenterX - ICON_W / 2, y - ICON_H / 2, fg);
            const int textX = fit.marginX + fit.rowW - fl.widthPx;
            drawFittedText(fl.line.text, textX, y, ML_DATUM, fl.line.sizeRole, fl.fontPx);
            y += lineH - lineH / 2;
        }
        drewAnyZone = true;
    }

    // ---- Action zone: caption, QR, scan, url -- all centered.
    bool hasAction = fit.qrScale > 0;
    for (int i = 0; i < fit.lineCount; i++) {
        LineKind k = fit.lines[i].line.kind;
        if (k == LineKind::Caption || k == LineKind::Scan || k == LineKind::Url) hasAction = true;
    }
    if (hasAction) {
        if (drewAnyZone) y += sectionGap;
        for (int i = 0; i < fit.lineCount; i++) {
            FitLine &fl = fit.lines[i];
            if (fl.line.kind != LineKind::Caption) continue;
            int lineH = (int)(fl.fontPx * LINE_HEIGHT);
            y += lineH / 2;
            drawFittedText(fl.line.text, cx, y, MC_DATUM, fl.line.sizeRole, fl.fontPx);
            y += lineH - lineH / 2;
        }
        if (fit.qrScale > 0) {
            y += (int)fit.qrGapPx;
            const int qrCy = y + fit.qrPx / 2;
            String payload = state == ScreenState::Onboarding
                ? "WIFI:S:" + String(AP_NAME) + ";;"
                : String(content.settingsUrl);
            drawQrCode(payload, cx, qrCy, fit.qrScale);
            y += fit.qrPx + (int)fit.qrGapPx;
        }
        for (int i = 0; i < fit.lineCount; i++) {
            FitLine &fl = fit.lines[i];
            if (fl.line.kind != LineKind::Scan && fl.line.kind != LineKind::Url) continue;
            int lineH = (int)(fl.fontPx * LINE_HEIGHT);
            y += lineH / 2;
            drawFittedText(fl.line.text, cx, y, MC_DATUM, fl.line.sizeRole, fl.fontPx);
            y += lineH - lineH / 2;
        }
        drewAnyZone = true;
    }

    // ---- Legend zone: keycap + text (full mode), or one centered line
    // (combined mode -- no single keycap applies).
    bool hasLegend = false;
    for (int i = 0; i < fit.lineCount; i++) if (fit.lines[i].line.kind == LineKind::Legend) hasLegend = true;
    if (hasLegend) {
        if (drewAnyZone) y += sectionGap;
        for (int i = 0; i < fit.lineCount; i++) {
            FitLine &fl = fit.lines[i];
            if (fl.line.kind != LineKind::Legend) continue;
            int lineH = (int)(fl.fontPx * LINE_HEIGHT);
            y += lineH / 2;
            if (fl.line.keyLabel[0]) {
                const int colCenterX = fit.marginX + (fit.rowW - fl.widthPx) / 2;
                drawKeycap(fl.line.keyLabel, colCenterX, y, fl.fontPx, TFT_BLACK);
                const int textX = fit.marginX + fit.rowW - fl.widthPx;
                drawFittedText(fl.line.text, textX, y, ML_DATUM, fl.line.sizeRole, fl.fontPx);
            } else {
                drawFittedText(fl.line.text, cx, y, MC_DATUM, fl.line.sizeRole, fl.fontPx);
            }
            y += lineH - lineH / 2;
        }
    }

    epaper.setTextDatum(TL_DATUM);
}

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    ScreenContent content = gatherLiveContent(vbatMv, deltaMv, haveDelta);
    snapshotPrevious();
    PortraitScope portrait;
    drawFrameScreen(ScreenState::Normal, content);
    epaper.update();
}
```

- [ ] **Step 3: Update `main.cpp`'s `runStatusMode()`** -- `drawStatusScreen()` now calls `epaper.update()` itself; the caller must stop doing it a second time

Replace (`src/main.cpp:79-85`):
```cpp
static bool runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    drawStatusScreen(vbatMv, deltaMv, haveDelta);
    devLog.println("updating panel (takes ~20-30 s)...");
    setLed(LedMode::Heartbeat);
    epaper.update();
    setLed(LedMode::Solid);
    devLog.println("done");
    if (!connectWifi()) return false; // provisioning fallback already drew
```
with:
```cpp
static bool runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    devLog.println("drawing status screen (takes ~20-30 s)...");
    setLed(LedMode::Heartbeat);
    drawStatusScreen(vbatMv, deltaMv, haveDelta); // draws + epaper.update()
    setLed(LedMode::Solid);
    devLog.println("done");
    if (!connectWifi()) return false; // provisioning fallback already drew
```

- [ ] **Step 4: Confirm `ui.cpp`'s share of the build failure is gone**

`net.cpp` is still on its old, now-broken implementation at this point — `pio run -e ee02` is still expected to fail, but only for reasons in `net.cpp`, not `ui.cpp`/`main.cpp` anymore:

```bash
pio run -e ee02 2>&1 | grep -E "error:" | grep -v "net.cpp"
```
Expected: no output.

- [ ] **Step 5: Add includes to `src/net.cpp`**

Add near the top of `src/net.cpp` (after the existing `#include "net.h"` block):
```cpp
#include "power.h"
#include "ui.h"
```

- [ ] **Step 6: Replace `showProvisioningScreen()`**

Replace (`src/net.cpp:68-100`):
```cpp
// First-boot / stale-credentials instructions. Everything centered and
// sized from the panel so it renders in every rotation and panel size
// (proportional y anchors from LayoutMetrics — see layout_math.h). Phones
// join the open hotspot from the first QR; the captive-portal page usually
// pops up by itself.
static void showProvisioningScreen() {
    const LayoutMetrics lm = currentLayout();
    const int cx = epaper.width() / 2;
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextSize(lm.bodySize);
    epaper.drawString("Wi-Fi setup", cx, lm.provTitleY, 4);
    epaper.drawString("1. Scan to join the frame's hotspot:", cx, lm.provStep1Y, 4);
    drawQrCode("WIFI:S:" + String(AP_NAME) + ";;", cx, lm.provQr1Y, lm.provQrScale);
    epaper.setTextSize(lm.smallSize);
    epaper.drawString("(or join \"" + String(AP_NAME) + "\" manually)",
                      cx, lm.provJoinManualY, 4);
    epaper.setTextSize(lm.bodySize);
    epaper.drawString("2. A setup page opens by itself.", cx, lm.provStep2Y, 4);
    epaper.setTextSize(lm.smallSize);
    epaper.drawString("If it doesn't, scan this or visit http://192.168.4.1:",
                      cx, lm.provQrHintY, 4);
    drawQrCode("http://192.168.4.1", cx, lm.provQr2Y, lm.provQrScale);
    epaper.setTextSize(lm.bodySize);
    epaper.drawString("3. Pick your 2.4 GHz network.", cx, lm.provStep3Y, 4);
    epaper.setTextSize(lm.smallSize);
    epaper.drawString("Change or forget it later: press KEY1, open Settings.",
                      cx, lm.provChangeY, 4);
    epaper.setTextDatum(TL_DATUM);
    epaper.update();
}
```
with:
```cpp
// First-boot / stale-credentials instructions -- the unified frame's
// Onboarding state. Battery is read fresh (no Wi-Fi/NVS metadata exists
// yet before a connection); Wi-Fi/next-fetch stay "--" (see the design
// spec's per-state field table).
static void showProvisioningScreen() {
    ScreenContent content{};
    content.batteryPct = batteryPercent(readBatteryMv());
    strncpy(content.wifiBase, "--", sizeof(content.wifiBase) - 1);
    strncpy(content.nextBase, "--", sizeof(content.nextBase) - 1);
    snapshotPrevious();
    PortraitScope portrait;
    drawFrameScreen(ScreenState::Onboarding, content);
    epaper.update();
}
```

- [ ] **Step 7: Build to verify -- this is the first fully green board build since Task 2**

Run: `pio run -e ee02 -e ee03 -e ee04 -e ee05`
Expected: SUCCESS on all four. Every consumer of the old `LayoutMetrics`/`computeLayout()` API and the removed vector-icon functions has now been rewritten.

- [ ] **Step 8: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "ui: drawFrameScreen() renderer; drawStatusScreen() becomes a thin wrapper"
git add src/net.cpp
git commit -m "net: showProvisioningScreen() becomes a unified-frame wrapper"
```

---

### Task 6: Error wrapper (`src/display.cpp`)

**Files:**
- Modify: `src/display.cpp` (`showError()`)

**Interfaces:**
- Consumes: `ScreenState`/`ScreenContent`/`drawFrameScreen`/`gatherLiveContent` (`ui.h`, Task 5), `PortraitScope` (`display.h`, Task 4), `lastVbatMv` (`state.h`).

- [ ] **Step 1: Add include**

Add near the top of `src/display.cpp` (after the existing include block):
```cpp
#include "ui.h"
```

- [ ] **Step 2: Replace `showError()`**

Replace (`src/display.cpp`, the final function):
```cpp
// Full-panel error screen (calls update()). Only drawn when someone is
// watching (button-initiated actions) — unattended wakes keep the photo.
void showError(const String &msg) {
    const int cx = epaper.width() / 2, cy = epaper.height() / 2;
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextSize(2);
    epaper.setTextColor(TFT_RED, TFT_WHITE);
    epaper.drawString("Something went wrong", cx, cy - 100, 4);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString(msg, cx, cy, 4);
    epaper.setTextSize(1);
    epaper.drawString("Check your Wi-Fi, then press KEY2 to try again.",
                      cx, cy + 90, 4);
    epaper.setTextDatum(TL_DATUM);
    epaper.update();
}
```
with:
```cpp
// Full-panel error screen. Only drawn when someone is watching
// (button-initiated actions) — unattended wakes keep the photo.
void showError(const String &msg) {
    ScreenContent content = gatherLiveContent(lastVbatMv, 0, false);
    strncpy(content.wifiBase, "failed", sizeof(content.wifiBase) - 1);
    strncpy(content.errorMsg, msg.c_str(), sizeof(content.errorMsg) - 1);
    snapshotPrevious();
    PortraitScope portrait;
    drawFrameScreen(ScreenState::Error, content);
    epaper.update();
}
```

- [ ] **Step 3: Build to verify**

Run: `pio run -e ee02`
Expected: SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/display.cpp
git commit -m "display: showError() becomes a unified-frame wrapper"
```

---

### Task 7: Full verification (all envs, native tests, EE02 hardware)

**Files:** none (verification only)

- [ ] **Step 1: Run every native test suite**

Run: `pio test -e native`
Expected: PASS — `test_icons`, `test_layout_math`, `test_status_content`, plus the pre-existing `test_battery`/`test_wifi_strength`/`test_quiet_hours`/`test_url_template`/`test_validate`/`test_ring_log`/`test_bmp_thumbnail` (unaffected by this change) all pass.

- [ ] **Step 2: Build all four board envs**

Run: `pio run -e ee02 -e ee03 -e ee04 -e ee05`
Expected: SUCCESS for all four. This is the only verification EE03/EE04/EE05 get in this change — no hardware for those exists to flash and check visually, and that limitation is reported as-is, not implied away (per this plan's Global Constraints).

- [ ] **Step 3: Flash EE02 and verify on real hardware**

Run: `pio run -e ee02 -t upload`

Then, with the device connected and on Wi-Fi, check all three screens:
- Press KEY1: confirm the status screen shows in portrait, with battery/Wi-Fi/next stats (icon + value), the settings QR (scan it to confirm it resolves), and the KEY1/KEY2/KEY3 legend with keycap glyphs.
- Trigger onboarding (forget the saved Wi-Fi network via the settings portal, or hold the reset behavior documented in `README.md`): confirm the "Join ... to set up Wi-Fi" screen renders in portrait with a scannable hotspot QR.
- Trigger an error (e.g. temporarily point `imageUrl` at an unreachable host via the settings portal, then press KEY2): confirm the error screen shows the failure reason folded into the instruction line, plus battery/Wi-Fi/next stats.
- Confirm the next scheduled photo fetch/display still renders in the user's *configured* rotation (not forced portrait) — this is the regression `PortraitScope`'s restore-on-scope-exit exists to prevent.

- [ ] **Step 4: Report results honestly**

State plainly: EE02 was flashed and visually verified on real hardware (all three screens + rotation restore). EE03/EE04/EE05 build successfully and are covered by the same pure-logic tests every board runs, but were not flashed or visually checked — no such hardware is available in this session.
