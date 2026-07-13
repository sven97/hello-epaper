#include "ui.h"
#include "config.h"
#include "display.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include <WiFi.h>
#include <time.h>

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

const char *wakeReasonHuman() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "scheduled refresh";
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t bits = esp_sleep_get_ext1_wakeup_status();
            if (bits & (1ULL << BTN_INFO))    return "KEY1 (settings)";
            if (bits & (1ULL << BTN_NEW_PIC)) return "KEY2 (new picture)";
            if (bits & (1ULL << BTN_PIN))     return "KEY3 (pin)";
            return "button";
        }
        default: return "power on";
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

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    // Metadata comes from recordFetchMetadata(); this page only reads it.
    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String wifiDesc = prefs.getString("wifiDesc", "?");
    String lastIp = prefs.getString("lastIp", "");

    String lines[5];
    int n = 0;
    lines[n++] = settings.name + "  —  woke by " + wakeReasonHuman();

    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm lastTm;
        localtime_r(&lastEpoch, &lastTm);
        char dow[16], hm[8];
        strftime(dow, sizeof(dow), "%a %b", &lastTm);
        strftime(hm, sizeof(hm), "%H:%M", &lastTm);
        lines[n++] = "last photo: " + String(dow) + " " +
                     String(lastTm.tm_mday) + " " + String(hm);
    } else {
        lines[n++] = "last photo: --";
    }

    if (held) {
        lines[n++] = "next photo: pinned (KEY3 resumes)";
    } else if (time(nullptr) > CLOCK_SANE_EPOCH) {
        time_t next = time(nullptr) + (time_t)plannedSleepSecs();
        struct tm nextTm;
        localtime_r(&next, &nextTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &nextTm);
        lines[n++] = "next photo: " + String(hm);
    } else {
        lines[n++] = "next photo: --";
    }

    lines[n++] = "Wi-Fi: " + wifiDesc;

    String batt = "battery: " + String(batteryPercent(vbatMv)) + "% (" +
                  String(vbatMv / 1000.0f, 2) + " V";
    if (haveDelta && deltaMv >= 20) batt += ", charging";
    batt += ")";
    lines[n++] = batt;

    const int cx = epaper.width() / 2;
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextSize(2);
    const int lineH = 90;
    // Info block in the upper half; QR + legend below it.
    int y = epaper.height() / 4 - ((n - 1) * lineH) / 2 + 60;
    for (int i = 0; i < n; i++, y += lineH)
        epaper.drawString(lines[i], cx, y, 4);

    // Caption + URL directly above the QR so the three read as one unit.
    String url = portalUrl();
    epaper.drawString("Scan to open settings", cx, y, 4); // still size 2
    epaper.setTextSize(1);
    epaper.drawString(url + (lastIp.isEmpty() ? "" : "   (" + lastIp + ")"),
                      cx, y + 55, 4);

    // Center the QR in the free band below the URL line, scaled so its
    // full extent (33 modules + 4-module quiet zone each side) fits.
    const int legendTop = epaper.height() - 200;
    const int bandTop = y + 85;
    const int bandBottom = legendTop - 30;
    const int qrCy = (bandTop + bandBottom) / 2;
    const int scale = min(8, (bandBottom - bandTop) / (33 + 8));
    drawQrCode(url, cx, qrCy, scale);

    epaper.drawString("KEY1: back to photo — closes settings", cx, legendTop, 4);
    epaper.drawString("KEY2: new picture", cx, legendTop + 55, 4);
    epaper.drawString(held ? "KEY3: unpin — refreshes resume"
                           : "KEY3: pin this picture",
                      cx, legendTop + 110, 4);
    epaper.setTextDatum(TL_DATUM);
}
