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
            snprintf(buf, sizeof(buf), "%d%% - %s", content.batteryPct, content.batteryVoltage);
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
            snprintf(buf, sizeof(buf), "%s - %s", content.wifiBase, content.wifiSsid);
        else
            snprintf(buf, sizeof(buf), "%s", content.wifiBase);
        setLine(out[n], LineKind::Stat, SizeRole::Stat, buf);
        out[n].icon = StatIcon::Wifi;
        n++;
    }
    if (cfg.showNextStat && n < maxOut) {
        char buf[48];
        if (cfg.statDetail && content.lastFetch[0])
            snprintf(buf, sizeof(buf), "%s - %s", content.nextBase, content.lastFetch);
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
        snprintf(combinedBuf, sizeof(combinedBuf), "KEY1 Config - KEY2 Retry - KEY3 Pin");
    } else {
        legendPhrase[0] = "Configuration"; legendPhrase[1] = "Refresh"; legendPhrase[2] = "Pin";
        snprintf(combinedBuf, sizeof(combinedBuf), "KEY1 Config - KEY2 Refresh - KEY3 Pin");
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
        for (int i = 0; i < lineCount && statCount < 3; i++)
            if (lines[i].kind == LineKind::Stat)
                statItemsRough[statCount++] = FitItem{lines[i].text, rowW};
        int statSize = statCount
            ? fitSize(SizeRole::Stat, statItemsRough, statCount, measure)
            : STAT_SIZES[STAT_SIZES_N - 1];
        float reserve = statSize * ICON_COL_RESERVE_FACTOR;

        FitItem statItems[3];
        for (int i = 0; i < statCount && i < 3; i++) {
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
