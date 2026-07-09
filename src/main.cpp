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
constexpr uint8_t LED_PIN = 21; // active-LOW

const char *AP_NAME = "EE02-Setup";
// picsum serves progressive JPEGs, which embedded decoders can't parse;
// images.weserv.nl re-encodes to baseline. The random= value defeats
// weserv's cache so every fetch is a different picture.
String imageUrl() {
    return "https://images.weserv.nl/?url=picsum.photos/1200/1600"
           "%3Frandom%3D" + String(esp_random()) + "&output=jpg";
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

// Connect with saved credentials, or open the captive portal on first
// boot / after forget. Blocks until connected or portal timeout.
bool connectWifi() {
    provisioningScreenShown = false; // each attempt may open a fresh portal
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(300); // give up after 5 min, don't hang forever
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
void ditherToPanel(const uint16_t *fb, int w, int h) {
    Serial.println("dithering to 6-color palette...");
    const int stride = (w + 2) * 3; // per-channel error, 1-px guard each side
    int16_t *errs = (int16_t *)calloc(2 * stride, sizeof(int16_t));
    if (!errs) {
        Serial.println("dither buffer alloc failed");
        return;
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
    ditherToPanel(fb, w, h);
    free(fb);
    return true;
}

bool fetchAndShowImage() {
    digitalWrite(LED_PIN, LOW);
    WiFiClientSecure client;
    client.setInsecure(); // learning repo: skip cert validation
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    String url = imageUrl();
    http.begin(client, url);
    http.setTimeout(20000);
    Serial.printf("GET %s\n", url.c_str());
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("HTTP error: %d\n", code);
        http.end();
        showError("HTTP " + String(code) + " from image server");
        digitalWrite(LED_PIN, HIGH);
        return false;
    }
    int len = http.getSize();
    Serial.printf("content-length: %d\n", len);
    if (len <= 0) {
        http.end();
        showError("server sent no Content-Length");
        digitalWrite(LED_PIN, HIGH);
        return false;
    }
    uint8_t *buf = (uint8_t *)ps_malloc(len); // PSRAM
    if (!buf) {
        http.end();
        showError("PSRAM alloc failed");
        digitalWrite(LED_PIN, HIGH);
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
    Serial.printf("received %u / %d bytes\n", (unsigned)got, len);
    if (got < (size_t)len) {
        free(buf);
        showError("download incomplete");
        digitalWrite(LED_PIN, HIGH);
        return false;
    }

    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(buf, got);
    free(buf);
    if (!rendered) {
        showError("jpeg decode failed");
        digitalWrite(LED_PIN, HIGH);
        return false;
    }
    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done — press refresh (GPIO3) for a new image");
    digitalWrite(LED_PIN, HIGH);
    return true;
}

bool pressed(uint8_t pin) {
    if (digitalRead(pin) == LOW) {
        delay(30);
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) delay(10);
            return true;
        }
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("ee02-playground: task 4 — wifi provisioning + picsum fetch");

    pinMode(BTN_PREV, INPUT);    // external pull-ups on board
    pinMode(BTN_REFRESH, INPUT);
    pinMode(BTN_NEXT, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    epaper.begin();

    if (digitalRead(BTN_REFRESH) == LOW) {
        Serial.println("refresh held at boot — forgetting saved wifi");
        WiFiManager wm;
        wm.resetSettings();
    }

    if (!connectWifi()) {
        showError("wifi setup failed or timed out");
        return;
    }
    fetchAndShowImage();
}

void loop() {
    if (pressed(BTN_REFRESH)) {
        if (WiFi.status() != WL_CONNECTED && !connectWifi()) {
            showError("wifi reconnect failed");
            return;
        }
        fetchAndShowImage();
    }
    delay(10);
}
