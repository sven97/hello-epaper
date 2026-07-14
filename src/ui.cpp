#include "ui.h"
#include "config.h"
#include "display.h"
#include "layout.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
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
    prefs.putString("wifiDesc",
                    WiFi.SSID() + " " + String(WiFi.RSSI()) + "dBm");
}

// One line each for last/next photo (combined -- was two lines), Wi-Fi,
// battery. The device name and wake reason used to be a fourth line here;
// dropped to make room for the title/subtitle block below, since the name
// still appears in the URL under the QR and the wake reason wasn't
// actionable information for someone reading the panel.
static void statusInfoLines(int32_t vbatMv, int32_t deltaMv, bool haveDelta,
                            String out[3]) {
    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String wifiDesc = prefs.getString("wifiDesc", "?");

    String last = "last: --";
    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm lastTm;
        localtime_r(&lastEpoch, &lastTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &lastTm);
        last = "last: " + String(hm);
    }
    String next;
    if (held) {
        next = "next: pinned";
    } else if (time(nullptr) > CLOCK_SANE_EPOCH) {
        time_t nextT = time(nullptr) + (time_t)plannedSleepSecs();
        struct tm nextTm;
        localtime_r(&nextT, &nextTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &nextTm);
        next = "next: " + String(hm);
    } else {
        next = "next: --";
    }
    out[0] = last + "  ·  " + next;
    out[1] = "Wi-Fi: " + wifiDesc;

    String batt = "battery: " + String(batteryPercent(vbatMv)) + "% (" +
                  String(vbatMv / 1000.0f, 2) + " V";
    if (haveDelta && deltaMv >= 20) batt += ", charging";
    batt += ")";
    out[2] = batt;
}

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    // Metadata comes from recordFetchMetadata(); this page only reads it.
    String lastIp = prefs.getString("lastIp", "");
    String info[3];
    statusInfoLines(vbatMv, deltaMv, haveDelta, info);
    String url = portalUrl();

    const LayoutMetrics lm = currentLayout();
    const bool large = lm.bodySize == 2;
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);

    // Title (bold) + subtitle (regular) -- left column in landscape, the
    // single centered column in portrait.
    epaper.setFreeFont(large ? &FreeSansBold24pt7b : &FreeSansBold12pt7b);
    epaper.drawString("Hello ePaper", lm.leftCx, lm.titleY);
    epaper.setFreeFont(large ? &FreeSans18pt7b : &FreeSans9pt7b);
    epaper.drawString(String("XIAO ePaper Display Board ") + BOARD_MODEL,
                      lm.leftCx, lm.subtitleY);

    // Info lines: bold, classic Font4 (unchanged weight/mechanism).
    epaper.setTextSize(lm.bodySize);
    epaper.drawString(info[0], lm.leftCx, lm.info0Y, 4);
    epaper.drawString(info[1], lm.leftCx, lm.info1Y, 4);
    epaper.drawString(info[2], lm.leftCx, lm.info2Y, 4);

    // Chrome text (caption/url/legend): unified size, regular weight,
    // smaller than the subtitle -- large tier uses a regular FreeSans face,
    // small tier falls back to the classic Font2 bitmap font (no FreeSans
    // size below 9pt is vendored, and 9pt is already the subtitle's size).
    if (large) {
        epaper.setFreeFont(&FreeSans12pt7b);
        epaper.drawString("Scan to open settings", lm.rightCx, lm.captionY);
        epaper.drawString(url + (lastIp.isEmpty() ? "" : "   (" + lastIp + ")"),
                          lm.rightCx, lm.urlY);
        epaper.drawString("KEY1: back to photo — closes settings", lm.rightCx, lm.legend0Y);
        epaper.drawString("KEY2: new picture", lm.rightCx, lm.legend1Y);
        epaper.drawString(held ? "KEY3: unpin — refreshes resume"
                               : "KEY3: pin this picture",
                          lm.rightCx, lm.legend2Y);
    } else {
        epaper.setTextSize(1);
        epaper.drawString("Scan to open settings", lm.rightCx, lm.captionY, 2);
        epaper.drawString(url + (lastIp.isEmpty() ? "" : "   (" + lastIp + ")"),
                          lm.rightCx, lm.urlY, 2);
        epaper.drawString("KEY1: back to photo — closes settings", lm.rightCx, lm.legend0Y, 2);
        epaper.drawString("KEY2: new picture", lm.rightCx, lm.legend1Y, 2);
        epaper.drawString(held ? "KEY3: unpin — refreshes resume"
                               : "KEY3: pin this picture",
                          lm.rightCx, lm.legend2Y, 2);
    }

    drawQrCode(url, lm.rightCx, lm.qrCy, lm.qrScale);
    epaper.setTextDatum(TL_DATUM);
}
