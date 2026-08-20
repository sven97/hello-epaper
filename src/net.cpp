#include "net.h"
#include "config.h"
#include "display.h"
#include "screencapture.h"
#include "devlog.h"
#include "photocache.h"
#include "portal.h"
#include "power.h"
#include "state.h"
#include "settings.h"
#include "ui.h"
#include "logic/url_template.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <time.h>
#include <cstring>

// The image source is a user-configurable URL template; {seed} defeats
// upstream caches per fetch, {width}/{height} follow the panel rotation.
// Default: weserv re-encode of picsum (embedded decoders need baseline
// JPEG, weserv converts progressive -> baseline at exact panel size).
static String imageUrl() {
    std::string u = renderUrlTemplate(settings.imageUrl.c_str(), esp_random(),
                                      epaper.width(), epaper.height());
    return String(u.c_str());
}

// Sink for HTTPClient::writeToStream() that grows a PSRAM buffer as data
// arrives. getSize()/Content-Length can't gate the download: a server is
// free to omit it entirely, either via chunked transfer-encoding or by
// just closing the connection when the body ends (both common for an
// on-the-fly generated image, e.g. a NAS resizing on request).
// writeToStream() is what actually knows how to de-chunk and detect
// end-of-body correctly for all of those framings -- getSize() alone
// only covers the one case where Content-Length was sent up front.
class PsramSink : public Stream {
public:
    ~PsramSink() { if (buf) free(buf); }
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *data, size_t n) override {
        if (len + n > cap) {
            size_t newCap = cap ? cap : (size_t)64 * 1024;
            while (newCap < len + n) newCap *= 2;
            if (newCap > MAX_BYTES) { setWriteError(); tooLarge = true; return 0; }
            uint8_t *grown = (uint8_t *)ps_realloc(buf, newCap);
            if (!grown) { setWriteError(); return 0; }
            buf = grown;
            cap = newCap;
        }
        memcpy(buf + len, data, n);
        len += n;
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

    uint8_t *buf = nullptr;
    size_t len = 0, cap = 0;
    bool tooLarge = false;
    // No panel this firmware supports needs anywhere near this much for
    // one baseline JPEG frame -- guards a runaway/malformed chunked
    // stream against exhausting PSRAM.
    static constexpr size_t MAX_BYTES = 16UL * 1024 * 1024;
};

// First-boot / stale-credentials instructions -- the unified frame's
// Onboarding state. Battery is read fresh (no Wi-Fi/NVS metadata exists
// yet before a connection); Wi-Fi/next-fetch stay "--" (see the design
// spec's per-state field table).
static void showProvisioningScreen() {
    ScreenContent content{};
    content.batteryPct = batteryPercent(readBatteryMv());
    strncpy(content.wifiBase, "--", sizeof(content.wifiBase) - 1);
    strncpy(content.nextBase, "--", sizeof(content.nextBase) - 1);
    snapshotPrevious();
    PortraitScope portrait;
    drawFrameScreen(ScreenState::Onboarding, content);
    epaper.update();
}

static bool provisioningScreenShown = false;

static void showProvisioningScreenOnce() {
    if (provisioningScreenShown) return;
    provisioningScreenShown = true;
    devLog.println("drawing provisioning instructions (takes ~20-30 s)...");
    showProvisioningScreen();
    devLog.println("instructions on panel");
}

static void configModeCallback(WiFiManager *wm) {
    devLog.printf("config portal up: join \"%s\", then open http://%s\n",
                  AP_NAME, WiFi.softAPIP().toString().c_str());
    // Fallback draw (saved credentials went stale, so the pre-draw in
    // connectWifi() was skipped). This callback fires before the portal
    // web server starts, so the ~30 s draw delays the portal — accepted:
    // the user can't follow instructions they haven't seen yet, and by
    // the time the panel shows them the portal is up.
    showProvisioningScreenOnce();
}

bool connectWifi(bool allowPortal, bool forceRadioReset) {
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
    if (forceRadioReset) {
        // A previous attempt this outage already failed against a radio
        // that was never reset -- power-cycle it before retrying, in case
        // something (e.g. a stale cached BSSID/channel hint) is stuck
        // rather than the saved credentials themselves being wrong.
        // Credentials are untouched -- only the radio driver state.
        devLog.println("wifi: power-cycling radio before retry...");
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_STA);
    }
    devLog.println("connecting (saved credentials, or captive portal)...");
    bool ok = wm.autoConnect(AP_NAME);
    if (ok) {
        devLog.printf("connected to %s, IP %s, RSSI %d dBm\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        prefs.putString("lastIp", WiFi.localIP().toString());
    } else {
        devLog.println("wifi connect failed");
    }
    return ok;
}

// Fetch the image into the framebuffer (no update() yet — the caller
// decides when to refresh). On failure fills err with a short
// user-facing message and draws nothing.
bool fetchImage(String &err) {
    String url = imageUrl();
    // Match the client to the URL's scheme: WiFiClientSecure always
    // TLS-handshakes on connect() regardless of scheme, so handing it a
    // plain http:// URL breaks against non-TLS servers (e.g. a NAS).
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    secureClient.setInsecure(); // learning repo: skip cert validation
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (url.startsWith("https://")) {
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }
    http.setTimeout(20000);
    devLog.printf("GET %s\n", url.c_str());
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        err = "image server said HTTP " + String(code);
        return false;
    }
    PsramSink sink;
    int written = http.writeToStream(&sink);
    http.end();
    if (written < 0) {
        err = sink.tooLarge ? "image too large for available memory"
                            : "image download failed: " + http.errorToString(written);
        return false;
    }
    if (sink.len == 0) {
        err = "image server sent no data";
        return false;
    }
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(sink.buf, sink.len);
    if (!rendered) {
        err = "that URL is not a baseline JPEG";
        return false;
    }
    savePhotoCache(sink.buf, sink.len); // best-effort: a failed cache
                                        // write doesn't fail the fetch
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
            devLog.printf("timezone: %s (UTC offset %+ld s)\n",
                          name.c_str(), off);
            prefs.putLong("tzOff", off);
            return off;
        }
    } else {
        http.end();
    }
    long cached = prefs.getLong("tzOff", 0);
    devLog.printf("timezone: detect failed (HTTP %d), cached offset %+ld s\n",
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
