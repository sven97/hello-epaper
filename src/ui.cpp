#include "ui.h"
#include "config.h"
#include "display.h"
#include "icons.h"
#include "screencapture.h"
#include "photocache.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "logic/wifi_strength.h"
#include "logic/quiet_hours.h"
#include <WiFi.h>
#include <time.h>
#include <cstring>
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

static int localDaySecs(time_t t) {
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

ScreenContent gatherLiveContent(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    (void)deltaMv; (void)haveDelta; // no charging indication in this design
    ScreenContent content{};
    content.batteryPct = batteryPercent(vbatMv);

    const int rssi = prefs.getInt("wifiRssi", -100);
    strncpy(content.wifiBase, wifiStrengthLabel(wifiStrengthBucket(rssi)),
            sizeof(content.wifiBase) - 1);
    String ssid = prefs.getString("wifiSsid", "");
    strncpy(content.wifiSsid, ssid.c_str(), sizeof(content.wifiSsid) - 1);

    // Next refresh, relative: plannedSleepSecs() is already quiet-adjusted;
    // formatNextRefresh renders "in Xh Ym" / "Pinned" / "Paused until HH:00".
    const time_t now = time(nullptr);
    const bool clockSane = now > CLOCK_SANE_EPOCH;
    const bool inQuiet = settings.quietEnabled && clockSane &&
        inQuietWindow(localDaySecs(now), settings.quietStartHour, settings.quietEndHour);
    formatNextRefresh(content.nextBase, sizeof(content.nextBase), held, clockSane,
                      inQuiet, settings.quietEndHour, plannedSleepSecs());

    strncpy(content.settingsUrl, portalUrl().c_str(), sizeof(content.settingsUrl) - 1);
    strncpy(content.lastIp, prefs.getString("lastIp", "").c_str(),
            sizeof(content.lastIp) - 1);
    deviceIdFromMac(content.deviceId, sizeof(content.deviceId), ESP.getEfuseMac());
    return content;
}

// ---- Fixed-grid renderer -------------------------------------------------
// Every font size the grid uses is already compiled in (see the header
// comment above). No fitting: role -> font is a fixed map.
enum class Role { Title, Big, Subhead, Body, Small };

static void useFont(Role r) {
    switch (r) {
        case Role::Title:   epaper.setFreeFont(&FreeSansBold24pt7b); break;
        case Role::Big:     epaper.setFreeFont(&FreeSansBold18pt7b); break;
        case Role::Subhead: epaper.setFreeFont(&FreeSansBold12pt7b); break;
        case Role::Body:    epaper.setFreeFont(&FreeSans12pt7b);     break;
        case Role::Small:   epaper.setFreeFont(&FreeSans9pt7b);      break;
    }
}

// Nominal line advance per role (px) -- tuned for vertical rhythm, not
// measured; the panel has ample vertical slack. See Task 6.
static int lineH(Role r) {
    switch (r) {
        case Role::Title:   return 60;
        case Role::Big:     return 46;
        case Role::Subhead: return 34;
        case Role::Body:    return 30;
        default:            return 24;
    }
}

static void text(const char *s, int x, int y, Role r, uint8_t datum = TL_DATUM) {
    if (!s || !s[0]) return;
    epaper.setTextDatum(datum);
    epaper.setTextSize(1);
    useFont(r);
    epaper.drawString(s, x, y);
}

static const uint8_t *batteryIcon(int pct) {
    return pct >= 75 ? ICON_BATTERY_FULL
         : pct >= 50 ? ICON_BATTERY_MEDIUM
         : pct >= 25 ? ICON_BATTERY_LOW
                     : ICON_BATTERY_EMPTY;
}

static const uint8_t *wifiIcon(const char *base) {
    return strcmp(base, "strong") == 0 ? ICON_WIFI_FULL
         : strcmp(base, "fair") == 0   ? ICON_WIFI_MEDIUM
         : strcmp(base, "weak") == 0   ? ICON_WIFI_LOW
                                        : ICON_WIFI_NONE;
}

// Spectra-6 self-test strip -- the one place colour is drawn on this screen.
static void drawColorSwatch(int x, int y, int cellW, int cellH) {
    static const uint16_t COLS[6] = {
        TFT_BLACK, TFT_WHITE, TFT_YELLOW, TFT_RED, TFT_BLUE, TFT_GREEN};
    for (int i = 0; i < 6; i++) {
        const int cx = x + i * cellW;
        epaper.fillRect(cx, y, cellW, cellH, COLS[i]);
        epaper.drawRect(cx, y, cellW, cellH, TFT_BLACK); // outline so the white cell reads
    }
}

// Draws the status/onboarding/error card as an overlay -- the caller has
// already painted the background (the retained image, or white). Only the
// 960x1056 card rectangle is touched.
void drawFrameScreen(ScreenState state, const ScreenContent &content) {
    const StatusData d = buildStatusData(state, content, FW_BUILD_NUMBER, AP_NAME,
                                         PANEL_DESC, PANEL_W, PANEL_H);

    epaper.setTextColor(TFT_BLACK, TFT_WHITE);

    // ---- Card: opaque white panel + 2px rounded border, centred.
    const int winX = GRID_MARGIN_X;
    const int winY = GRID_MARGIN_Y;
    epaper.fillRoundRect(winX, winY, GRID_WIN_W, GRID_WIN_H, GRID_WIN_RADIUS, TFT_WHITE);
    epaper.drawRoundRect(winX, winY, GRID_WIN_W, GRID_WIN_H, GRID_WIN_RADIUS, TFT_BLACK);
    epaper.drawRoundRect(winX + 1, winY + 1, GRID_WIN_W - 2, GRID_WIN_H - 2,
                         GRID_WIN_RADIUS - 1, TFT_BLACK);

    const int ox = winX + GRID_PAD;          // content-box left
    const int leftX = ox + gridColX(1);
    const int rightX = ox + gridColX(7);     // right half-window
    auto rule = [&](int yy) {
        epaper.drawFastHLine(ox, yy, GRID_CONTENT_W, TFT_BLACK);
    };

    int y = winY + GRID_PAD;

    // ---- Header
    text(d.title, leftX, y, Role::Title);
    y += lineH(Role::Title);
    text(d.versionLine, leftX, y, Role::Small);
    y += lineH(Role::Small) + GRID_ZONE_PAD;
    rule(y);
    y += GRID_ZONE_PAD;

    // ---- Status band: next-refresh (left cols 1-6), battery + Wi-Fi (right 7-12)
    {
        text(d.nextLabel, leftX, y, Role::Small);
        text(d.nextValue, leftX, y + lineH(Role::Small), Role::Big);

        const int r0 = y;
        epaper.drawBitmap(rightX, r0, batteryIcon(d.batteryPct), ICON_W, ICON_H, TFT_BLACK);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", d.batteryPct);
        text(pct, rightX + ICON_W + 14, r0 + 8, Role::Body);

        const int r1 = r0 + ICON_H + 16;
        text(d.wifiHeading, rightX, r1, Role::Subhead);
        const int r2 = r1 + lineH(Role::Subhead);
        epaper.drawBitmap(rightX, r2, wifiIcon(d.wifiBase), ICON_W, ICON_H, TFT_BLACK);
        text(d.wifiLabel, rightX + ICON_W + 14, r2 + 8, Role::Body);

        y = r2 + ICON_H + GRID_ZONE_PAD;
    }
    rule(y);
    y += GRID_ZONE_PAD;

    // ---- Action: QR (left cols 1-5), instructions + URLs (right cols 6-12)
    {
        const int qrBoxW = gridSpanW(1, 5);
        const int scale = qrScaleForBox(qrBoxW, qrBoxW);
        const int qrPx = QR_TOTAL_MODULES * scale;
        const int qrCx = ox + gridColX(1) + qrBoxW / 2;
        const int qrCy = y + qrPx / 2;
        drawQrCode(String(d.qrPayload), qrCx, qrCy, scale);

        const int tx = ox + gridColX(6);
        text(d.actionHeading, tx, y, Role::Subhead);
        text(d.actionLine1, tx, y + 44, Role::Body);
        text(d.actionLine2, tx, y + 44 + lineH(Role::Body), Role::Body);
        text(d.urlPrimary, tx, y + 44 + 2 * lineH(Role::Body) + 12, Role::Body);
        if (d.urlSecondary[0])
            text(d.urlSecondary, tx, y + 44 + 3 * lineH(Role::Body) + 14, Role::Small);

        const int textH = 44 + 3 * lineH(Role::Body) + 14 + lineH(Role::Small);
        y += (qrPx > textH ? qrPx : textH) + GRID_ZONE_PAD;
    }
    rule(y);
    y += GRID_ZONE_PAD;

    // ---- Device: id (left cols 1-6), panel spec + swatch (right 7-12)
    {
        text(d.deviceIdLabel, leftX, y, Role::Small);
        text(d.deviceId, leftX, y + lineH(Role::Small), Role::Subhead);

        text(d.panelDesc, rightX, y, Role::Body);
        text(d.resLine, rightX, y + lineH(Role::Body), Role::Body);
        drawColorSwatch(rightX, y + 2 * lineH(Role::Body) + 8, 44, 34);

        y += 2 * lineH(Role::Body) + 8 + 34 + GRID_ZONE_PAD;
    }
    rule(y);
    y += GRID_ZONE_PAD;

    // ---- Legend: thirds (cols 1-4 / 5-8 / 9-12), keycap + phrase
    {
        const int startCol[3] = {1, 5, 9};
        for (int i = 0; i < 3; i++) {
            if (!d.legend[i] || !d.legend[i][0]) continue;
            const int cx = ox + gridColX(startCol[i]);
            char digit[2] = {char('1' + i), '\0'};
            drawKeycap(digit, cx + 18, y + 16, 34, TFT_BLACK);
            text(d.legend[i], cx + 46, y + 16, Role::Body, ML_DATUM);
        }
    }

    epaper.setTextDatum(TL_DATUM);
}

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    ScreenContent content = gatherLiveContent(vbatMv, deltaMv, haveDelta);
    snapshotPrevious();
    PortraitScope portrait;
    // The card sits over the retained image -- KEY1 toggles it without
    // changing what's displayed. renderCachedPhoto() repaints the sprite
    // with the current image; white only if there's no cache yet.
    if (!renderCachedPhoto()) epaper.fillScreen(TFT_WHITE);
    drawFrameScreen(ScreenState::Normal, content);
    epaper.update();
}
