#include "portal.h"
#include "config.h"
#include "portal_html.h"
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

static String rotOptions() {
    static const char *LABELS[4] = {"Portrait", "Landscape",
                                    "Portrait (flipped)",
                                    "Landscape (flipped)"};
    String out;
    for (int r = 0; r < 4; r++)
        out += "<option value=\"" + String(r) + "\"" +
               (r == settings.rotation ? " selected" : "") + ">" + LABELS[r] +
               "</option>";
    return out;
}

// One-glance device state under the heading: battery, and when the next
// photo lands (omitted before the first NTP sync — never show 1970 math).
static String statusLine() {
    String s = "battery " + String(batteryPercent(lastVbatMv)) + "%";
    if (held) return s + " · pinned";
    if (time(nullptr) > CLOCK_SANE_EPOCH) {
        time_t next = time(nullptr) + (time_t)plannedSleepSecs();
        struct tm t;
        localtime_r(&next, &t);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &t);
        s += " · next photo ";
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

static String buildPage(const String &error) {
    String page = FPSTR(PORTAL_HTML);
    page.replace("%ERROR%",
                 error.isEmpty() ? "" : "<div class=\"msg\">" + error + "</div>");
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
    // Must be last: a stored URL containing a literal token string (e.g.
    // "%PAUSED%") must not be re-substituted by a later replace() call.
    page.replace("%URL%", htmlEscape(settings.imageUrl));
    return page;
}

static void sendDone(const String &title, const String &body) {
    String page = FPSTR(PORTAL_DONE_HTML);
    page.replace("%TITLE%", title);
    page.replace("%BODY%", body);
    server.send(200, "text/html", page);
}

static void handleRoot() {
    lastActivityMs = millis();
    server.send(200, "text/html", buildPage(""));
}

// Validate everything before writing anything: a rejected form must leave
// NVS untouched.
static void handleSave() {
    lastActivityMs = millis();
    uint32_t sleep = (uint32_t)server.arg("sleep").toInt();
    String url = server.arg("url");
    bool paused = server.hasArg("paused");
    bool quietEn = server.hasArg("quiet_en");
    int quietStart = server.arg("quiet_start").toInt();
    int quietEnd = server.arg("quiet_end").toInt();
    String tz = server.arg("tz");
    String name = server.arg("name");
    int rot = server.arg("rot").toInt();

    String err;
    if (!isValidSleepSecs(sleep)) err = "Invalid refresh interval.";
    else if (!isValidImageUrl(url.c_str())) err = "Image URL must be http(s) and under 512 chars.";
    else if (!isValidHour(quietStart) || !isValidHour(quietEnd)) err = "Invalid quiet hours.";
    else if (quietEn && quietStart == quietEnd) err = "Quiet hours start and end must differ.";
    else if (!isValidDeviceName(name.c_str())) err = "Name: 1-24 of a-z, 0-9, hyphen (not at the ends).";
    else if (!isValidRotation(rot)) err = "Invalid orientation.";
    else if (tz != "auto" && !isValidTzOffsetSec(tz.toInt())) err = "Invalid timezone offset.";
    if (!err.isEmpty()) {
        server.send(400, "text/html", buildPage(err));
        return;
    }

    settings.sleepSecs = sleep;
    settings.imageUrl = url;
    settings.quietEnabled = quietEn;
    settings.quietStartHour = (uint8_t)quietStart;
    settings.quietEndHour = (uint8_t)quietEnd;
    settings.name = name;
    settings.rotation = (uint8_t)rot;
    settings.tzAuto = (tz == "auto");
    if (!settings.tzAuto) prefs.putLong("tzOff", tz.toInt());
    saveSettings();
    prefs.putBool("held", paused);
    held = paused;

    sendDone("Saved", "The frame is applying settings and fetching a picture — the panel takes ~30 s to refresh.<p class=\"note\">To open settings again later, press KEY1 on the frame.</p>");
    result = PortalResult::Saved;
    exitRequested = true;
    Serial.println("portal: settings saved");
}

static void handleNewPic() {
    lastActivityMs = millis();
    sendDone("Fetching", "New picture on the way — the panel takes ~30 s to refresh.<p class=\"note\">To open settings again later, press KEY1 on the frame.</p>");
    result = PortalResult::Saved;
    exitRequested = true;
    Serial.println("portal: new picture requested");
}

static void handleForgetWifi() {
    lastActivityMs = millis();
    sendDone("Wi-Fi forgotten",
             "The frame will show setup instructions on its screen. Join the <b>" + String(AP_NAME) + "</b> hotspot to reconnect.");
    delay(300); // let the response reach the phone BEFORE dropping Wi-Fi
    WiFiManager wm;
    wm.resetSettings(); // disconnects STA — must come after the send
    result = PortalResult::ForgetWifi;
    exitRequested = true;
    Serial.println("portal: wifi credentials forgotten");
}

bool startPortal() {
    if (!MDNS.begin(settings.name.c_str()))
        Serial.println("portal: mDNS failed (IP still works)");
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/action/newpic", HTTP_POST, handleNewPic);
    server.on("/action/forgetwifi", HTTP_POST, handleForgetWifi);
    server.onNotFound(
        []() { server.send(404, "text/plain", "not found"); });
    server.begin();
    Serial.printf("portal: %s (http://%s)\n", portalUrl().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
}

PortalResult runPortal(uint32_t inactivityTimeoutMs) {
    result = PortalResult::Timeout;
    exitRequested = false;
    lastActivityMs = millis();
    while (!exitRequested) {
        server.handleClient();
        if (buttonPressed(BTN_INFO)) {
            result = PortalResult::KeyExit;
            break;
        }
        if (millis() - lastActivityMs > inactivityTimeoutMs) break;
        delay(10);
    }
    delay(200); // let the last HTTP response flush
    server.stop();
    MDNS.end();
    return result;
}
