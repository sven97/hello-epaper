#include "ui.h"
#include "config.h"
#include "display.h"
#include "icons.h"
#include "screencapture.h"
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
