#include "net.h"
#include "config.h"
#include "display.h"
#include "portal.h"
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

// First-boot / stale-credentials instructions. Everything centered and
// sized from the panel so it renders in every rotation; all y <= 1150
// fits the 1200 px landscape height. Phones join the open hotspot from
// the first QR; the captive-portal page usually pops up by itself.
static void showProvisioningScreen() {
    const int cx = epaper.width() / 2;
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextSize(2);
    epaper.drawString("Wi-Fi setup", cx, 90, 4);
    epaper.drawString("1. Scan to join the frame's hotspot:", cx, 210, 4);
    drawQrCode("WIFI:S:" + String(AP_NAME) + ";;", cx, 400, 4);
    epaper.setTextSize(1);
    epaper.drawString("(or join \"" + String(AP_NAME) + "\" manually)",
                      cx, 545, 4);
    epaper.setTextSize(2);
    epaper.drawString("2. A setup page opens by itself.", cx, 660, 4);
    epaper.setTextSize(1);
    epaper.drawString("If it doesn't, scan this or visit http://192.168.4.1:",
                      cx, 725, 4);
    drawQrCode("http://192.168.4.1", cx, 880, 4);
    epaper.setTextSize(2);
    epaper.drawString("3. Pick your 2.4 GHz network.", cx, 1060, 4);
    epaper.setTextSize(1);
    epaper.drawString("Change or forget it later: press KEY1, open Settings.",
                      cx, 1140, 4);
    epaper.setTextDatum(TL_DATUM);
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

bool connectWifi(bool allowPortal) {
    provisioningScreenShown = false; // each attempt may open a fresh portal
    // Both this firmware's settings portal and WiFiManager's captive
    // portal bind port 80 — free ours in case provisioning must open.
    if (allowPortal) stopPortal();
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(300);
    wm.setEnableConfigPortal(allowPortal);
    // No saved credentials means the portal WILL open: draw the instructions
    // now, before autoConnect(), so the portal web server isn't blocked
    // behind the ~30 s panel draw when the user tries to reach it.
    if (allowPortal && !wm.getWiFiIsSaved()) showProvisioningScreenOnce();
    Serial.println("connecting (saved credentials, or captive portal)...");
    bool ok = wm.autoConnect(AP_NAME);
    if (ok) {
        Serial.printf("connected to %s, IP %s, RSSI %d dBm\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        prefs.putString("lastIp", WiFi.localIP().toString());
    } else {
        Serial.println("wifi connect failed");
    }
    return ok;
}

// Fetch the image into the framebuffer (no update() yet — the caller
// decides when to refresh). On failure fills err with a short
// user-facing message and draws nothing.
bool fetchImage(String &err) {
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
        err = "image server said HTTP " + String(code);
        return false;
    }
    int len = http.getSize();
    if (len <= 0) {
        http.end();
        err = "image server sent no size";
        return false;
    }
    uint8_t *buf = (uint8_t *)ps_malloc(len);
    if (!buf) {
        http.end();
        err = "out of memory for the image";
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
        err = "image download was cut off";
        return false;
    }
    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(buf, got);
    free(buf);
    if (!rendered) {
        err = "that URL is not a baseline JPEG";
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
