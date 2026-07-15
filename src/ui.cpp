#include "ui.h"
#include "config.h"
#include "display.h"
#include "layout.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "logic/battery_curve.h"
#include "logic/wifi_strength.h"
#include <WiFi.h>
#include <time.h>
// FreeSansBold24pt7b/FreeSansBold12pt7b/FreeSans18pt7b/FreeSans9pt7b/
// FreeSans12pt7b are already pulled in unconditionally by Seeed_GFX's
// gfxfont.h (via <TFT_eSPI.h>, included through display.h) whenever
// LOAD_GFXFF is defined -- these headers have no include guards, so
// including them again here would double-define their font data.

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
    // Stored separately (not one pre-formatted string) so the status
    // screen's caption row (SSID) and tile row (RSSI bucket) can each
    // read only what they need.
    prefs.putString("wifiSsid", WiFi.SSID());
    prefs.putInt("wifiRssi", WiFi.RSSI());
}

// "next photo" tile value ("HH:MM", "pinned", or "--"), plus whether it's
// the pinned state (drives the pin-icon swap in drawStatusScreen).
static String nextPhotoValue(bool *pinnedOut) {
    *pinnedOut = held;
    if (held) return "pinned";
    if (time(nullptr) <= CLOCK_SANE_EPOCH) return "--";
    time_t nextT = time(nullptr) + (time_t)plannedSleepSecs();
    struct tm t;
    localtime_r(&nextT, &t);
    char hm[8];
    strftime(hm, sizeof(hm), "%H:%M", &t);
    return String(hm);
}

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    // Metadata comes from recordFetchMetadata(); this page only reads it.
    const LayoutMetrics lm = currentLayout();
    const bool large = lm.bodySize == 2;

    const int pct = batteryPercent(vbatMv);
    const BatteryLevel battLevel = batteryLevelBucket(pct);
    const bool charging = haveDelta && deltaMv >= 20;

    const int rssi = prefs.getInt("wifiRssi", -100);
    const WifiStrength wifiLevel = wifiStrengthBucket(rssi);
    const String ssid = prefs.getString("wifiSsid", "?");

    bool pinned = false;
    const String nextValue = nextPhotoValue(&pinned);

    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String lastStr = "--";
    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm t;
        localtime_r(&lastEpoch, &t);
        char buf[16];
        strftime(buf, sizeof(buf), "%a %H:%M", &t);
        lastStr = buf;
    }

    char voltBuf[8];
    snprintf(voltBuf, sizeof(voltBuf), "%.2fV", vbatMv / 1000.0f);
    const String caption = "last " + lastStr + "  ·  " + ssid + "  ·  " + voltBuf;

    const String url = portalUrl();
    const String lastIp = prefs.getString("lastIp", "");

    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);

    // Header: title centered; board-model badge right-anchored on the
    // same mid-line (a fixed corner anchor avoids needing textWidth() to
    // place it next to the title).
    epaper.setFreeFont(large ? &FreeSansBold24pt7b : &FreeSansBold12pt7b);
    epaper.drawString("Hello ePaper", lm.cx, lm.titleY);
    epaper.setTextDatum(MR_DATUM);
    if (large) {
        epaper.setFreeFont(&FreeSans12pt7b);
        epaper.drawString(BOARD_MODEL, epaper.width() - lm.marginX, lm.titleY);
    } else {
        epaper.setTextSize(1);
        epaper.drawString(BOARD_MODEL, epaper.width() - lm.marginX, lm.titleY, 2);
    }
    epaper.setTextDatum(MC_DATUM);

    // Divider rule under the header.
    epaper.fillRect(lm.marginX, lm.ruleY, epaper.width() - 2 * lm.marginX, 3, TFT_BLACK);

    // Tile row: battery / Wi-Fi / next photo, icon + big value + small label.
    const int iconR = lm.tileValueH / 2;
    drawBatteryIcon(lm.tile0Cx, lm.tileIconCy, iconR, pct,
                    batteryColorForLevel(battLevel), charging);
    drawWifiIcon(lm.tile1Cx, lm.tileIconCy + iconR, iconR, wifiLevel);
    drawNextPhotoIcon(lm.tile2Cx, lm.tileIconCy, iconR, pinned);

    epaper.setFreeFont(large ? &FreeSans18pt7b : &FreeSans9pt7b);
    epaper.drawString(String(pct) + "%", lm.tile0Cx, lm.tileValueY);
    epaper.drawString(wifiStrengthLabel(wifiLevel), lm.tile1Cx, lm.tileValueY);
    epaper.drawString(nextValue, lm.tile2Cx, lm.tileValueY);

    if (large) {
        epaper.setFreeFont(&FreeSans12pt7b);
        epaper.drawString("battery", lm.tile0Cx, lm.tileLabelY);
        epaper.drawString("Wi-Fi", lm.tile1Cx, lm.tileLabelY);
        epaper.drawString("next", lm.tile2Cx, lm.tileLabelY);
        epaper.drawString(caption, lm.cx, lm.captionY);
    } else {
        epaper.setTextSize(1);
        epaper.drawString("battery", lm.tile0Cx, lm.tileLabelY, 2);
        epaper.drawString("Wi-Fi", lm.tile1Cx, lm.tileLabelY, 2);
        epaper.drawString("next", lm.tile2Cx, lm.tileLabelY, 2);
        epaper.drawString(caption, lm.cx, lm.captionY, 2);
    }

    drawQrCode(url, lm.cx, lm.qrCy, lm.qrScale);

    if (large) {
        epaper.setFreeFont(&FreeSans12pt7b);
        epaper.drawString("Scan to open settings", lm.cx, lm.scanY);
        epaper.drawString(url + (lastIp.isEmpty() ? "" : "   (" + lastIp + ")"),
                          lm.cx, lm.urlY);
        epaper.drawString("KEY1: back to photo — closes settings", lm.cx, lm.legend0Y);
        epaper.drawString("KEY2: new picture", lm.cx, lm.legend1Y);
        epaper.drawString(held ? "KEY3: unpin — refreshes resume"
                               : "KEY3: pin this picture",
                          lm.cx, lm.legend2Y);
    } else {
        epaper.setTextSize(1);
        epaper.drawString("Scan to open settings", lm.cx, lm.scanY, 2);
        epaper.drawString(url + (lastIp.isEmpty() ? "" : "   (" + lastIp + ")"),
                          lm.cx, lm.urlY, 2);
        epaper.drawString("KEY1: back to photo — closes settings", lm.cx, lm.legend0Y, 2);
        epaper.drawString("KEY2: new picture", lm.cx, lm.legend1Y, 2);
        epaper.drawString(held ? "KEY3: unpin — refreshes resume"
                               : "KEY3: pin this picture",
                          lm.cx, lm.legend2Y, 2);
    }

    epaper.setTextDatum(TL_DATUM);
}
