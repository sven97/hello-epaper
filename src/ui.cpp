#include "ui.h"
#include "config.h"
#include "display.h"
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

void recordFetchMetadata() {
    // Don't stamp a never-synced clock as the fetch time — keep the
    // previous (accurate) epoch instead and let the page show that.
    time_t now = time(nullptr);
    if (now > CLOCK_SANE_EPOCH)
        prefs.putULong("lastEpoch", (uint32_t)now);
    prefs.putString("wifiDesc",
                    WiFi.SSID() + " " + String(WiFi.RSSI()) + "dBm");
}

// Centered lines, font 4 at 2x (~52 px tall). The charger (BQ24070) works
// autonomously and its status pins only drive the onboard LEDs, so
// charging ("chg") is inferred from the voltage rising between wakes.
void drawInfoScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    // Metadata comes from recordFetchMetadata(); this page only reads it.
    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String wifiDesc = prefs.getString("wifiDesc", "?");

    String lines[5];
    int n = 0;
    lines[n++] = "wake: " + String(wakeReason());

    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm lastTm;
        localtime_r(&lastEpoch, &lastTm);
        char dow[16], hm[8];
        strftime(dow, sizeof(dow), "%a %b", &lastTm);
        strftime(hm, sizeof(hm), "%H:%M", &lastTm);
        lines[n++] = "last: " + String(dow) + " " + String(lastTm.tm_mday) +
                     " " + String(hm);
    } else {
        lines[n++] = "last: --";
    }

    if (held) {
        lines[n++] = "next: held";
    } else {
        time_t next = time(nullptr) + (time_t)settings.sleepSecs;
        struct tm nextTm;
        localtime_r(&next, &nextTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &nextTm);
        lines[n++] = "next: " + String(hm);
    }

    lines[n++] = "wifi: " + wifiDesc;

    String batt = "batt: " + String(batteryPercent(vbatMv)) + "% (" +
                  String(vbatMv / 1000.0f, 2) + "V";
    if (haveDelta) {
        batt += ", ";
        if (deltaMv >= 0) batt += "+";
        batt += String(deltaMv) + "mV";
        if (deltaMv >= 20) batt += ", chg";
    }
    batt += ")";
    lines[n++] = batt;

    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextDatum(MC_DATUM); // strings centered on their anchor point
    epaper.setTextSize(2);
    const int lineH = 100;
    int y = epaper.height() / 2 - ((n - 1) * lineH) / 2; // vertically center the block
    for (int i = 0; i < n; i++, y += lineH)
        epaper.drawString(lines[i], epaper.width() / 2, y, 4);
    epaper.setTextSize(1);
    epaper.setTextDatum(TL_DATUM);
}
