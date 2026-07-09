#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <JPEGDecoder.h>

EPaper epaper;

constexpr uint8_t BTN_PREV = 2;
constexpr uint8_t BTN_REFRESH = 3;
constexpr uint8_t BTN_NEXT = 5;
constexpr uint64_t BUTTON_WAKE_MASK =
    (1ULL << BTN_PREV) | (1ULL << BTN_REFRESH) | (1ULL << BTN_NEXT);
constexpr uint8_t LED_PIN = 21;          // active-LOW
constexpr uint8_t BATTERY_ADC_PIN = 1;   // A0, via /2 divider
constexpr uint8_t BATTERY_EN_PIN = 6;    // HIGH enables the divider
constexpr uint8_t EPAPER_EN_PIN = 43;    // panel power enable
constexpr uint64_t SLEEP_SECONDS = 60 * 60; // 1 hour

const char *AP_NAME = "EE02-Setup";

// picsum serves progressive JPEGs, which embedded decoders can't parse;
// images.weserv.nl re-encodes to baseline. The random= value defeats
// weserv's cache so every fetch is a different picture.
String imageUrl() {
    return "https://images.weserv.nl/?url=picsum.photos/1200/1600"
           "%3Frandom%3D" + String(esp_random()) + "&output=jpg";
}

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR int32_t lastVbatMv = -1; // survives deep sleep, not reset/flash

// Battery voltage in millivolts, via the S3's eFuse-calibrated ADC
// (analogReadMilliVolts) — the naive raw/4095*3.3 conversion read 20-30 %
// low because the ADC saturates below 3.3 V at default attenuation.
int32_t readBatteryMv() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BATTERY_EN_PIN, OUTPUT);
    digitalWrite(BATTERY_EN_PIN, HIGH);
    delay(5);
    uint32_t sumMv = 0;
    for (int i = 0; i < 10; i++) {
        sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
        delay(2);
    }
    digitalWrite(BATTERY_EN_PIN, LOW);
    return (int32_t)(sumMv / 10) * 2; // /2 divider on the board
}

const char *wakeReason() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_EXT1:  return "button";
        default:                     return "power-on/reset";
    }
}

void showError(const String &msg) {
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_RED, TFT_WHITE);
    epaper.drawString("ERROR", 20, 40, 4);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString(msg, 20, 120, 4);
    epaper.update();
}

void showProvisioningScreen() {
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString("Wi-Fi setup", 20, 40, 4);
    epaper.drawString("1. On your phone, join the Wi-Fi network:", 20, 160, 4);
    epaper.setTextColor(TFT_BLUE, TFT_WHITE);
    epaper.drawString(AP_NAME, 60, 220, 4);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString("2. Open http://192.168.4.1 in a browser", 20, 300, 4);
    epaper.drawString("3. Pick your 2.4 GHz network, enter its password", 20, 360, 4);
    epaper.drawString("The board remembers it for future boots.", 20, 480, 4);
    epaper.drawString("Hold the refresh button at power-on to forget.", 20, 540, 4);
    epaper.update();
}

bool provisioningScreenShown = false;

void showProvisioningScreenOnce() {
    if (provisioningScreenShown) return;
    provisioningScreenShown = true;
    Serial.println("drawing provisioning instructions (takes ~20-30 s)...");
    showProvisioningScreen();
    Serial.println("instructions on panel");
}

void configModeCallback(WiFiManager *wm) {
    Serial.printf("config portal up: join \"%s\", then open http://%s\n",
                  AP_NAME, WiFi.softAPIP().toString().c_str());
    // Fallback draw (saved credentials went stale, so the pre-draw in
    // connectWifi() was skipped). This callback fires before the portal
    // web server starts, so the ~30 s draw delays the portal — accepted:
    // the user can't follow instructions they haven't seen yet, and by
    // the time the panel shows them the portal is up.
    showProvisioningScreenOnce();
}

bool connectWifi() {
    provisioningScreenShown = false; // each attempt may open a fresh portal
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(300);
    // No saved credentials means the portal WILL open: draw the instructions
    // now, before autoConnect(), so the portal web server isn't blocked
    // behind the ~30 s panel draw when the user tries to reach it.
    if (!wm.getWiFiIsSaved()) showProvisioningScreenOnce();
    Serial.println("connecting (saved credentials, or captive portal)...");
    bool ok = wm.autoConnect(AP_NAME);
    if (ok) {
        Serial.printf("connected to %s, IP %s, RSSI %d dBm\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
    } else {
        Serial.println("provisioning timed out");
    }
    return ok;
}

// Spectra 6 palette: panel nibble index (drawPixel stores it directly at
// 4 bpp) + sRGB approximation used as the dithering target.
struct PaletteEntry { uint8_t idx; int16_t r, g, b; };
const PaletteEntry PALETTE[6] = {
    {0x0, 255, 255, 255}, // white
    {0xF, 0,   0,   0  }, // black
    {0x6, 255, 0,   0  }, // red
    {0xB, 255, 255, 0  }, // yellow
    {0x2, 0,   255, 0  }, // green
    {0xD, 0,   0,   255}, // blue
};

// Floyd-Steinberg dither the RGB565 frame down to the 6 panel colors.
// Raw RGB565 must never be pushed: at 4 bpp the sprite stores
// color & 0x0F, i.e. it expects palette nibbles, not RGB values.
bool ditherToPanel(const uint16_t *fb, int w, int h) {
    Serial.println("dithering to 6-color palette...");
    const int stride = (w + 2) * 3; // per-channel error, 1-px guard each side
    int16_t *errs = (int16_t *)calloc(2 * stride, sizeof(int16_t));
    if (!errs) {
        Serial.println("dither buffer alloc failed");
        return false;
    }
    int16_t *cur = errs, *next = errs + stride;
    for (int y = 0; y < h; y++) {
        memset(next, 0, stride * sizeof(int16_t));
        for (int x = 0; x < w; x++) {
            uint16_t c = fb[(size_t)y * w + x];
            int r = (c >> 8) & 0xF8; r |= r >> 5;
            int g = (c >> 3) & 0xFC; g |= g >> 6;
            int b = (c << 3) & 0xF8; b |= b >> 5;
            const int e = (x + 1) * 3;
            r += cur[e];     if (r < 0) r = 0; if (r > 255) r = 255;
            g += cur[e + 1]; if (g < 0) g = 0; if (g > 255) g = 255;
            b += cur[e + 2]; if (b < 0) b = 0; if (b > 255) b = 255;
            int best = 0;
            int32_t bestD = INT32_MAX;
            for (int i = 0; i < 6; i++) {
                int32_t dr = r - PALETTE[i].r;
                int32_t dg = g - PALETTE[i].g;
                int32_t db = b - PALETTE[i].b;
                int32_t d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = i; }
            }
            epaper.drawPixel(x, y, PALETTE[best].idx);
            int er = r - PALETTE[best].r;
            int eg = g - PALETTE[best].g;
            int eb = b - PALETTE[best].b;
            cur[e + 3]  += er * 7 / 16;
            cur[e + 4]  += eg * 7 / 16;
            cur[e + 5]  += eb * 7 / 16;
            next[e - 3] += er * 3 / 16;
            next[e - 2] += eg * 3 / 16;
            next[e - 1] += eb * 3 / 16;
            next[e]     += er * 5 / 16;
            next[e + 1] += eg * 5 / 16;
            next[e + 2] += eb * 5 / 16;
            next[e + 3] += er / 16;
            next[e + 4] += eg / 16;
            next[e + 5] += eb / 16;
        }
        int16_t *tmp = cur; cur = next; next = tmp;
    }
    free(errs);
    Serial.println("dithering done");
    return true;
}

// Decode the JPEG into a full RGB565 frame in PSRAM, then dither it to
// the panel. Returns false if the JPEG doesn't decode to sane dimensions.
bool renderJpeg(uint8_t *buf, size_t len) {
    JpegDec.decodeArray(buf, len);
    const int w = JpegDec.width, h = JpegDec.height;
    Serial.printf("jpeg: %d x %d, MCU %d x %d\n", w, h,
                  JpegDec.MCUWidth, JpegDec.MCUHeight);
    if (w <= 0 || h <= 0 || w > epaper.width() || h > epaper.height()) {
        JpegDec.abort();
        Serial.println("bad jpeg dimensions");
        return false;
    }
    uint16_t *fb = (uint16_t *)ps_malloc((size_t)w * h * sizeof(uint16_t));
    if (!fb) {
        JpegDec.abort();
        Serial.println("PSRAM alloc for frame failed");
        return false;
    }
    while (JpegDec.read()) {
        const int mx = JpegDec.MCUx * JpegDec.MCUWidth;
        const int my = JpegDec.MCUy * JpegDec.MCUHeight;
        const uint16_t *p = JpegDec.pImage;
        for (int row = 0; row < JpegDec.MCUHeight; row++) {
            if (my + row >= h) break;
            for (int col = 0; col < JpegDec.MCUWidth; col++) {
                if (mx + col >= w) continue;
                fb[(size_t)(my + row) * w + (mx + col)] =
                    p[row * JpegDec.MCUWidth + col];
            }
        }
    }
    bool ok = ditherToPanel(fb, w, h);
    free(fb);
    return ok;
}

// Fetch the image into the framebuffer (no update() yet — the caller
// overlays the status footer first). Returns false after drawing an
// error screen (already updated) on failure.
bool fetchImage() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    String url = imageUrl();
    http.begin(client, url);
    http.setTimeout(20000);
    Serial.printf("GET %s\n", url.c_str());
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        showError("HTTP " + String(code) + " from image server");
        return false;
    }
    int len = http.getSize();
    if (len <= 0) {
        http.end();
        showError("server sent no Content-Length");
        return false;
    }
    uint8_t *buf = (uint8_t *)ps_malloc(len);
    if (!buf) {
        http.end();
        showError("PSRAM alloc failed");
        return false;
    }
    WiFiClient *stream = http.getStreamPtr();
    size_t got = 0;
    uint32_t lastData = millis();
    while (got < (size_t)len && millis() - lastData < 20000) {
        size_t avail = stream->available();
        if (avail) {
            got += stream->readBytes(buf + got, min(avail, (size_t)len - got));
            lastData = millis();
        } else {
            delay(10);
        }
    }
    http.end();
    if (got < (size_t)len) {
        free(buf);
        showError("download incomplete");
        return false;
    }
    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(buf, got);
    free(buf);
    if (!rendered) {
        showError("jpeg decode failed");
        return false;
    }
    return true;
}

// The charger (BQ24070) works autonomously and its status pins only drive
// the onboard LEDs, so charging is inferred from the voltage trend across
// wakes: on charge, the measured voltage sits well above resting and rises.
void drawStatusFooter(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    String status = "boot #" + String(bootCount) + "  wake: " +
                    wakeReason() + "  vbat: " +
                    String(vbatMv / 1000.0f, 2) + " V";
    if (haveDelta) {
        status += " (";
        if (deltaMv >= 0) status += "+";
        status += String(deltaMv) + " mV";
        if (deltaMv >= 20) status += ", charging";
        status += ")";
    }
    epaper.fillRect(0, 1560, 1200, 40, TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString(status, 20, 1568, 2);
}

void goToSleep() {
    Serial.printf("sleeping %llu s (buttons also wake)...\n", SLEEP_SECONDS);
    Serial.flush();
    epaper.sleep();                    // panel low-power mode
    pinMode(EPAPER_EN_PIN, OUTPUT);    // cut panel power rail
    digitalWrite(EPAPER_EN_PIN, LOW);
    // Deep sleep floats the pads unless held: latch the enable lines low
    // so the panel and battery divider stay off while sleeping.
    gpio_hold_en((gpio_num_t)EPAPER_EN_PIN);
    gpio_hold_en((gpio_num_t)BATTERY_EN_PIN);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    bootCount++;
    Serial.printf("ee02-playground: task 5 — boot #%u, wake: %s\n",
                  bootCount, wakeReason());

    // Release the pin holds from the previous deep sleep (no-op on first
    // boot) so the panel and battery divider can be driven again.
    gpio_hold_dis((gpio_num_t)EPAPER_EN_PIN);
    gpio_hold_dis((gpio_num_t)BATTERY_EN_PIN);

    pinMode(BTN_REFRESH, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); // LED on while awake

    int32_t vbatMv = readBatteryMv();
    bool haveDelta = lastVbatMv >= 0;
    int32_t deltaMv = haveDelta ? vbatMv - lastVbatMv : 0;
    lastVbatMv = vbatMv;
    if (haveDelta)
        Serial.printf("battery: %.2f V (%+d mV since last wake)\n",
                      vbatMv / 1000.0f, (int)deltaMv);
    else
        Serial.printf("battery: %.2f V\n", vbatMv / 1000.0f);

    epaper.begin();

    // Hold refresh through power-on (still held after the 2 s boot delay)
    // to forget saved wifi. A short press that merely woke us from deep
    // sleep is released by now and does NOT trigger this.
    if (digitalRead(BTN_REFRESH) == LOW) {
        Serial.println("refresh held at boot — forgetting saved wifi");
        WiFiManager wm;
        wm.resetSettings();
    }

    if (!connectWifi()) {
        showError("wifi setup failed or timed out");
    } else if (fetchImage()) {
        drawStatusFooter(vbatMv, deltaMv, haveDelta);
        Serial.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        Serial.println("done");
    }

    digitalWrite(LED_PIN, HIGH);
    goToSleep(); // never returns
}

void loop() {}
