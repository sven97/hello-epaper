#include "portal.h"
#include "config.h"
#include "display.h"
#include "devlog.h"
#include "net.h"
#include "ota_state.h"
#include "photocache.h"
#include "portal_html.h"
#include "screencapture.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "logic/validate.h"
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <time.h>

static WebServer server(80);
static PortalResult result;
static bool exitRequested;
static uint32_t lastActivityMs;
static bool portalRunning;
static bool portalPersistent;

// Set by /set when a visible setting changed; consumed between
// server.handleClient() calls. pendingRender re-dithers the cached image
// (no network, handled here); pendingFetch pulls a fresh one and is
// taken by the KEY1 caller / dev loop via takePortalFetch().
static bool pendingRender = false;
static bool pendingFetch = false;

String portalUrl() { return "http://" + settings.name + ".local"; }

static String selectOptions(int from, int to, int selected,
                            const char *suffix) {
    String out;
    for (int v = from; v <= to; v++) {
        String label = (v < 10 ? "0" : "") + String(v) + suffix;
        out += "<option value=\"" + String(v) + "\"" +
               (v == selected ? " selected" : "") + ">" + label + "</option>";
    }
    return out;
}

static String sleepOptions(uint32_t selected) {
    struct Opt { uint32_t s; const char *label; };
    static const Opt OPTS[] = {{900, "15 min"},  {1800, "30 min"},
                               {3600, "1 h"},    {7200, "2 h"},
                               {14400, "4 h"},   {28800, "8 h"},
                               {43200, "12 h"},  {86400, "24 h"}};
    String out;
    for (auto &o : OPTS)
        out += "<option value=\"" + String(o.s) + "\"" +
               (o.s == selected ? " selected" : "") + ">" + o.label +
               "</option>";
    return out;
}

static String tzOptions() {
    // "auto" plus manual offsets in 15-min steps. Selected = current mode.
    long cur = prefs.getLong("tzOff", 0);
    String out = String("<option value=\"auto\"") +
                 (settings.tzAuto ? " selected" : "") +
                 ">Auto (IP geolocation)</option>";
    for (long o = -14L * 3600; o <= 14L * 3600; o += 900) {
        char label[16];
        snprintf(label, sizeof(label), "UTC%c%ld:%02ld", o < 0 ? '-' : '+',
                 labs(o) / 3600, (labs(o) % 3600) / 60);
        out += "<option value=\"" + String(o) + "\"" +
               (!settings.tzAuto && o == cur ? " selected" : "") + ">" +
               label + "</option>";
    }
    return out;
}

// Label by the panel's actual visual shape per rotation value, not a fixed
// rotation->label table: EE03/EE04/EE05's native panel is landscape-shaped
// (unlike EE02's portrait-native 1200x1600), so rotation 0 there produces a
// landscape image, not portrait -- see PANEL_NATIVE_LANDSCAPE in display.h.
static String rotOptions() {
    String out;
    for (int r = 0; r < 4; r++) {
        bool landscape = (r % 2 == 0) ? PANEL_NATIVE_LANDSCAPE
                                       : !PANEL_NATIVE_LANDSCAPE;
        String label = String(landscape ? "Landscape" : "Portrait") +
                      (r >= 2 ? " (flipped)" : "");
        out += "<option value=\"" + String(r) + "\"" +
               (r == settings.rotation ? " selected" : "") + ">" + label +
               "</option>";
    }
    return out;
}

// One-glance device state under the heading: battery, and when the next
// image lands (omitted before the first NTP sync — never show 1970 math).
static String statusLine() {
    String s = "battery " + String(batteryPercent(lastVbatMv)) + "%";
    if (held) return s + " · paused";
    if (time(nullptr) > CLOCK_SANE_EPOCH) {
        time_t next = time(nullptr) + (time_t)plannedSleepSecs();
        struct tm t;
        localtime_r(&next, &t);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &t);
        s += " · next image ";
        s += hm;
    }
    return s;
}

// Escape for HTML attribute/text context. Needed for the image URL: it is
// only validated for scheme + length, so a '"' would otherwise break out of
// the value="..." attribute and inject markup into every future render.
static String htmlEscape(const String &s) {
    String out = s;
    out.replace("&", "&amp;");
    out.replace("\"", "&quot;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    return out;
}

static String buildPage() {
    String page = FPSTR(PORTAL_HTML);
    page.replace("%NAME%", settings.name);
    page.replace("%STATUS%", statusLine());
    page.replace("%SLEEP_OPTS%", sleepOptions(settings.sleepSecs));
    page.replace("%PAUSED%", held ? "checked" : "");
    page.replace("%QUIET_EN%", settings.quietEnabled ? "checked" : "");
    page.replace("%QS_OPTS%",
                 selectOptions(0, 23, settings.quietStartHour, ":00"));
    page.replace("%QE_OPTS%",
                 selectOptions(0, 23, settings.quietEndHour, ":00"));
    page.replace("%TZ_OPTS%", tzOptions());
    page.replace("%ROT_OPTS%", rotOptions());
    page.replace("%OTA_EN%", settings.otaEnabled ? "checked" : "");
    page.replace("%HASH%", FW_GIT_HASH);
    page.replace("%BUILD%", String((uint32_t)FW_BUILD_NUMBER));
    // Must be last: a stored URL containing a literal token string (e.g.
    // "%PAUSED%") must not be re-substituted by a later replace() call.
    page.replace("%URL%", htmlEscape(settings.imageUrl));
    return page;
}

static void handleRoot() {
    lastActivityMs = millis();
    server.send(200, "text/html", buildPage());
}

// One field per request. Validates just that field; on rejection writes
// nothing and returns the message. Sets a pending-panel flag for the two
// fields that change what's on the display.
static void handleSet() {
    lastActivityMs = millis();
    String f = server.arg("f");
    String v = server.arg("v");
    bool on = (v == "1" || v == "true" || v == "on");
    String err;

    if (f == "sleep") {
        uint32_t s = (uint32_t)v.toInt();
        if (!isValidSleepSecs(s)) err = "Invalid interval.";
        else { settings.sleepSecs = s; saveSettings(); }
    } else if (f == "url") {
        if (!isValidImageUrl(v.c_str()))
            err = "Must be http(s) and under 512 characters.";
        else if (v != settings.imageUrl) {
            settings.imageUrl = v;
            saveSettings();
            pendingFetch = true;
        }
    } else if (f == "paused") {
        held = on;
        prefs.putBool("held", held);
    } else if (f == "quiet_en") {
        settings.quietEnabled = on;
        saveSettings();
    } else if (f == "quiet_start" || f == "quiet_end") {
        int h = v.toInt();
        if (!isValidHour(h)) err = "Invalid hour.";
        else {
            uint8_t s = settings.quietStartHour, e = settings.quietEndHour;
            if (f == "quiet_start") s = (uint8_t)h; else e = (uint8_t)h;
            if (settings.quietEnabled && s == e)
                err = "Start and end must differ.";
            else {
                settings.quietStartHour = s;
                settings.quietEndHour = e;
                saveSettings();
            }
        }
    } else if (f == "tz") {
        if (v == "auto") { settings.tzAuto = true; saveSettings(); }
        else if (!isValidTzOffsetSec(v.toInt())) err = "Invalid offset.";
        else {
            settings.tzAuto = false;
            saveSettings();
            prefs.putLong("tzOff", v.toInt());
        }
    } else if (f == "name") {
        if (!isValidDeviceName(v.c_str()))
            err = "1-24 of a-z, 0-9, hyphen (not at the ends).";
        else { settings.name = v; saveSettings(); }
    } else if (f == "rot") {
        int r = v.toInt();
        if (!isValidRotation(r)) err = "Invalid orientation.";
        else if ((uint8_t)r != settings.rotation) {
            settings.rotation = (uint8_t)r;
            saveSettings();
            pendingRender = true;
        }
    } else if (f == "ota_en") {
        settings.otaEnabled = on;
        saveSettings();
    } else {
        err = "Unknown field.";
    }

    if (err.isEmpty()) server.send(200, "text/plain", "ok");
    else               server.send(400, "text/plain", err);
}

static void handleOtaPeek() {
    lastActivityMs = millis();
    uint32_t latest = 0;
    switch (otaPeek(latest)) {
        case OtaPeekResult::Available:
            server.send(200, "text/plain", "available " + String(latest));
            return;
        case OtaPeekResult::UpToDate:
            server.send(200, "text/plain", "uptodate");
            return;
        case OtaPeekResult::Blocked:
            server.send(200, "text/plain", "blocked");
            return;
        default:
            server.send(200, "text/plain", "unreachable");
            return;
    }
}

static void handleOtaInstall() {
    lastActivityMs = millis();
    devLog.println("portal: manual firmware install");
    otaInstallNow(); // reboots on success
    server.send(200, "text/plain", "nothing newer to install");
}

static void handleFactoryReset() {
    lastActivityMs = millis();
    server.send(200, "text/plain", "erasing");
    devLog.println("portal: factory reset");
    delay(300); // let the response flush before the network drops
    WiFiManager wm;
    wm.resetSettings(); // Wi-Fi credentials live in a separate NVS namespace
    prefs.clear();      // wipes the whole "frame" namespace
    delay(100);
    ESP.restart();
}

static void handleLastJpg() { streamCachedPhoto(server); }
static void handleCurrent() { streamCurrentBmp(server); }
static void handlePrevious() { streamPreviousBmp(server); }

static void handleDebugKey1() { simulateButtonPress(BTN_INFO); server.send(200, "text/plain", "ok"); }
static void handleDebugKey2() { simulateButtonPress(BTN_NEW_PIC); server.send(200, "text/plain", "ok"); }
static void handleDebugKey3() { simulateButtonPress(BTN_PIN); server.send(200, "text/plain", "ok"); }

static void handleLog() {
    server.send(200, "text/plain", devLog.snapshot());
}

static void handleDebug() {
    String page = FPSTR(DEBUG_HTML);
    page.replace("%BOARD%", BOARD_MODEL);
    page.replace("%HASH%", FW_GIT_HASH);
    page.replace("%BUILD%", String((uint32_t)FW_BUILD_NUMBER));

    page.replace("%OTA_STATE%", settings.otaEnabled ? "on" : "off");

    OtaState os = otaStateLoad();
    String otaLast = "never";
    if (os.lastCheckEpoch > (uint32_t)CLOCK_SANE_EPOCH) {
        time_t t = (time_t)os.lastCheckEpoch;
        struct tm lt;
        localtime_r(&t, &lt);
        char buf[20];
        strftime(buf, sizeof(buf), "%m-%d %H:%M", &lt);
        otaLast = buf;
    }
    page.replace("%OTA_LAST%", otaLast);

    page.replace("%OTA_TRIAL%",
                 os.pendingBuild ? "build " + String(os.pendingBuild) +
                                       " (boot " + String(os.trialBoots) + ")"
                                 : String("none"));

    page.replace("%LOG%", htmlEscape(devLog.snapshot()));
    server.send(200, "text/html", page);
}

bool startPortal() {
    if (portalRunning) return true;
    if (!MDNS.begin(settings.name.c_str()))
        devLog.println("portal: mDNS failed (IP still works)");
    static bool routesRegistered = false;
    if (!routesRegistered) {
        routesRegistered = true;
        server.on("/", HTTP_GET, handleRoot);
        server.on("/set", HTTP_POST, handleSet);
        server.on("/ota/peek", HTTP_GET, handleOtaPeek);
        server.on("/ota/install", HTTP_POST, handleOtaInstall);
        server.on("/factory-reset", HTTP_POST, handleFactoryReset);
        server.on("/last.jpg", HTTP_GET, handleLastJpg);
        server.on("/current", HTTP_GET, handleCurrent);
        server.on("/previous", HTTP_GET, handlePrevious);
        server.on("/debug/key1", HTTP_POST, handleDebugKey1);
        server.on("/debug/key2", HTTP_POST, handleDebugKey2);
        server.on("/debug/key3", HTTP_POST, handleDebugKey3);
        server.on("/log", HTTP_GET, handleLog);
        server.on("/debug", HTTP_GET, handleDebug);
        server.onNotFound(
            []() { server.send(404, "text/plain", "not found"); });
    }
    server.begin();
    devLog.printf("portal: %s (http://%s)\n", portalUrl().c_str(),
                  WiFi.localIP().toString().c_str());
    portalRunning = true;
    return true;
}

// An orientation change from /set: re-dither the cached image at the new
// rotation (no network) and push it. The ~20-30 s draw blocks the portal;
// the JS shows "updating display…" from the /set 200 response. A url
// change (pendingFetch) needs an actual fetch and is taken by the caller
// via takePortalFetch() instead — portal.cpp can't see doFetchCycle().
static void servicePendingRender() {
    if (!pendingRender) return;
    pendingRender = false;
    applyOrientation();
    setLed(LedMode::Heartbeat);
    if (renderCachedPhoto()) {
        devLog.println("updating display (takes ~20-30 s)...");
        epaper.update();
    }
    setLed(LedMode::Solid);
    lastActivityMs = millis(); // the long draw isn't idleness
}

bool takePortalFetch() {
    bool f = pendingFetch;
    pendingFetch = false;
    return f;
}

PortalResult runPortal(uint32_t inactivityTimeoutMs) {
    result = PortalResult::Timeout;
    exitRequested = false;
    lastActivityMs = millis();
    while (!exitRequested) {
        server.handleClient();
        servicePendingRender();
        if (buttonPressed(BTN_INFO) || consumeSimulatedPress(BTN_INFO)) {
            result = PortalResult::KeyExit;
            break;
        }
        if (millis() - lastActivityMs > inactivityTimeoutMs) break;
        delay(10);
    }
    delay(200); // let the last HTTP response flush
    exitRequested = false;
    if (!portalPersistent) stopPortal();
    return result;
}

void setPortalPersistent(bool on) { portalPersistent = on; }

void servicePortal() {
    if (!portalRunning) return;
    server.handleClient();
    servicePendingRender();
}

void stopPortal() {
    if (!portalRunning) return;
    server.stop();
    MDNS.end();
    portalRunning = false;
}

bool portalIsRunning() { return portalRunning; }
