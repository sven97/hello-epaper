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
