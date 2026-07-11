#include "net.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include "settings.h"
#include "logic/url_template.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <time.h>

// The image source is a user-configurable URL template; {seed} defeats
// upstream caches per fetch, {width}/{height} follow the panel rotation.
// Default: weserv re-encode of picsum (embedded decoders need baseline
// JPEG, weserv converts progressive -> baseline at exact panel size).
static String imageUrl() {
    std::string u = renderUrlTemplate(settings.imageUrl.c_str(), esp_random(),
                                      epaper.width(), epaper.height());
    return String(u.c_str());
}

// Layout note: all y-coordinates stay under 1200 so the screen renders
// in both portrait (1600 tall) and landscape (1200 tall).
static void showProvisioningScreen() {
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
    epaper.drawString("Change or forget it later: press KEY1, open Settings.", 20, 540, 4);
    epaper.update();
}

static bool provisioningScreenShown = false;

static void showProvisioningScreenOnce() {
    if (provisioningScreenShown) return;
    provisioningScreenShown = true;
    Serial.println("drawing provisioning instructions (takes ~20-30 s)...");
    showProvisioningScreen();
    Serial.println("instructions on panel");
}

static void configModeCallback(WiFiManager *wm) {
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
        prefs.putString("lastIp", WiFi.localIP().toString());
    } else {
        Serial.println("provisioning timed out");
    }
    return ok;
}

// Fetch the image into the framebuffer (no update() yet — the caller
// decides when to refresh). Returns false after drawing an error screen
// (already updated) on failure.
bool fetchImage() {
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

void applyUtcOffset(long offsetSec) {
    // POSIX TZ strings invert the sign: UTC+8 is written "UTC-8".
    char tz[24];
    long p = -offsetSec;
    snprintf(tz, sizeof(tz), "UTC%+ld:%02ld", p / 3600, labs(p % 3600) / 60);
    setenv("TZ", tz, 1);
    tzset();
}

// Timezone is detected from the network's public IP on every fetch wake, so
// DST changes and even relocating the frame self-correct within an hour.
// The offset is a fixed value per wake (no DST rules needed — the API
// already applied them). Cached in NVS for wakes where the API is down.
static long detectUtcOffset() {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, TZ_API_URL);
    http.setTimeout(8000);
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        String body = http.getString();
        http.end();
        int o = body.indexOf("\"offset\":");
        if (body.indexOf("success") >= 0 && o >= 0) {
            long off = body.substring(o + 9).toInt();
            int t = body.indexOf("\"timezone\":\"");
            String name = "?";
            if (t >= 0) {
                int e = body.indexOf('"', t + 12);
                name = body.substring(t + 12, e);
            }
            Serial.printf("timezone: %s (UTC offset %+ld s)\n",
                          name.c_str(), off);
            prefs.putLong("tzOff", off);
            return off;
        }
    } else {
        http.end();
    }
    long cached = prefs.getLong("tzOff", 0);
    Serial.printf("timezone: detect failed (HTTP %d), cached offset %+ld s\n",
                  code, cached);
    return cached;
}

// One NTP sync per wake — the RTC drifts and deep sleep is long, and we're
// online anyway. In manual timezone mode the ip-api call is skipped
// entirely (privacy: no geolocation; also works on offline-only LANs).
bool syncClock() {
    long off = settings.tzAuto ? detectUtcOffset() : prefs.getLong("tzOff", 0);
    configTime(off, 0, "pool.ntp.org");
    struct tm now;
    return getLocalTime(&now, 10000);
}
