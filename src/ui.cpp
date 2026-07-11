#include "ui.h"
#include "config.h"
#include "display.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include <WiFi.h>
#include <qrcode.h>
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

// QR code for the portal URL. Version 4 (33x33 modules) fits
// "http://<24-char-name>.local" at ECC_LOW with room to spare.
static void drawQr(const String &text, int cx, int cy, int scale) {
    QRCode qr;
    uint8_t data[qrcode_getBufferSize(4)];
    if (qrcode_initText(&qr, data, 4, ECC_LOW, text.c_str()) != 0) return;
    const int px = qr.size * scale;
    const int x0 = cx - px / 2, y0 = cy - px / 2;
    // Quiet zone: 4 modules of white on every side.
    epaper.fillRect(x0 - 4 * scale, y0 - 4 * scale, px + 8 * scale,
                    px + 8 * scale, TFT_WHITE);
    for (int y = 0; y < qr.size; y++)
        for (int x = 0; x < qr.size; x++)
            if (qrcode_getModule(&qr, x, y))
                epaper.fillRect(x0 + x * scale, y0 + y * scale, scale, scale,
                                TFT_BLACK);
}

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    // Metadata comes from recordFetchMetadata(); this page only reads it.
    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String wifiDesc = prefs.getString("wifiDesc", "?");
    String lastIp = prefs.getString("lastIp", "");

    String lines[5];
    int n = 0;
    lines[n++] = settings.name + "  —  wake: " + String(wakeReason());

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
        lines[n++] = "next: pinned";
    } else {
        time_t next = time(nullptr) + (time_t)plannedSleepSecs();
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

    // Portal URL at base text size (a full URL + IP overflows size 2).
    String url = portalUrl();
    epaper.setTextSize(1);
    epaper.drawString("settings: " + url +
                          (lastIp.isEmpty() ? "" : "  (" + lastIp + ")"),
                      cx, y, 4);

    // Center the QR in the free band between the URL line and the legend,
    // scaled so its full extent (33 modules + 4-module quiet zone each
    // side) fits the band in either rotation.
    const int legendTop = epaper.height() - 200;
    const int bandTop = y + 40;
    const int bandBottom = legendTop - 30;
    const int qrCy = (bandTop + bandBottom) / 2;
    const int scale = min(8, (bandBottom - bandTop) / (33 + 8));
    drawQr(url, cx, qrCy, scale);

    // Button legend with live state — the frame documents itself.
    epaper.drawString("KEY1: back to photo (settings portal is on while "
                      "this page shows)", cx, legendTop, 4);
    epaper.drawString("KEY2: new picture now", cx, legendTop + 55, 4);
    epaper.drawString(held ? "KEY3: unpin — currently pinned"
                           : "KEY3: pin current picture",
                      cx, legendTop + 110, 4);
    epaper.setTextDatum(TL_DATUM);
}
