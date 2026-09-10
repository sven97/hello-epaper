#pragma once
// Content model for the fixed-grid status / onboarding / error screen.
// Pure logic: host-testable, no Arduino deps. See
// docs/superpowers/specs/2026-09-09-paperframe-ee02-only-status-redesign-design.md.
//
// buildStatusData() resolves every zone to ready-to-place strings; the
// renderer (ui.cpp) draws each at its fixed grid slot -- there is no
// fitting step.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "layout_math.h"

enum class ScreenState : uint8_t { Normal, Onboarding, Error };

// Which layer failed -- the Error state renders these differently: a Wi-Fi
// failure blanks the signal row, an image failure keeps it (the network is
// fine, the image source isn't).
enum class ErrorKind : uint8_t { Wifi, Image };

// Everything a caller (ui.cpp / net.cpp / display.cpp) gathers per draw.
struct ScreenContent {
    int  batteryPct = 0;
    char wifiBase[16] = {0};    // "strong"/"fair"/"weak"/"--"/"failed" -- icon selector
    char wifiSsid[32] = {0};    // "" when unknown
    char nextBase[24] = {0};    // pre-formatted by formatNextRefresh()
    char settingsUrl[48] = {0}; // portalUrl(), e.g. "http://paperframe.local"
    char lastIp[24] = {0};      // "192.168.1.42"; "" when never connected
    char deviceId[12] = {0};    // "PF-A82F"
    char errorMsg[64] = {0};    // Error-state instruction text; "" elsewhere
    ErrorKind errorKind = ErrorKind::Wifi; // Error state only
};

// Resolved strings for every zone. `const char *` fields point either at
// string literals or into the caller's ScreenContent -- valid only as
// long as that ScreenContent outlives the StatusData, which it always
// does (the renderer uses it immediately). Formatted fields are owned.
struct StatusData {
    const char *title = "Paperframe";
    char        versionLine[32] = {0}; // "Firmware build 1247"

    const char *nextLabel = "Next image refresh";
    char        nextValue[24] = {0};

    int         batteryPct = 0;
    const char *wifiHeading = "";      // SSID, or "Not connected" / "Connection failed"
    char        wifiBase[16] = {0};    // signal-strength icon selector, copied through

    const char *actionHeading = "Open settings";
    const char *actionLine1 = "";
    const char *actionLine2 = "";
    char        urlPrimary[48] = {0};
    char        urlSecondary[40] = {0}; // "Or http://192.168.1.42"; "" if none

    const char *deviceIdLabel = "Device ID";
    char        deviceId[12] = {0};
    const char *panelDesc = "";
    char        resLine[16] = {0};      // "1200 x 1600"

    const char *legend[3] = {"", "", ""}; // "" for a blank slot

    char        qrPayload[64] = {0};
};

// "next image refresh" value: "Pinned" when held; "Paused until HH:00"
// when a quiet-hours window is suppressing wakes (needs a sane clock for
// the hour); otherwise a relative "in Xh Ym" / "in Xm" / "< 1 min" from
// the already quiet-adjusted sleep budget.
inline void formatNextRefresh(char *out, size_t cap, bool held, bool clockSane,
                              bool inQuiet, int quietEndHour, uint32_t sleepSecs) {
    if (held) { snprintf(out, cap, "Pinned"); return; }
    if (inQuiet && clockSane) {
        snprintf(out, cap, "Paused until %02d:00", ((quietEndHour % 24) + 24) % 24);
        return;
    }
    if (sleepSecs < 60) { snprintf(out, cap, "< 1 min"); return; }
    const unsigned mins = (unsigned)(sleepSecs / 60);
    if (mins < 60) { snprintf(out, cap, "in %um", mins); return; }
    snprintf(out, cap, "in %uh %um", mins / 60, mins % 60);
}

// Stable per-device id from the low 16 bits of the factory MAC.
inline void deviceIdFromMac(char *out, size_t cap, uint64_t mac) {
    snprintf(out, cap, "PF-%04X", (unsigned)(mac & 0xFFFFu));
}

inline StatusData buildStatusData(ScreenState state, const ScreenContent &c,
                                  uint32_t buildNumber, const char *apName,
                                  const char *panelDesc, int panelW, int panelH) {
    StatusData d{};

    snprintf(d.versionLine, sizeof(d.versionLine), "Firmware build %u",
             (unsigned)buildNumber);
    d.batteryPct = c.batteryPct;
    strncpy(d.wifiBase, c.wifiBase, sizeof(d.wifiBase) - 1);
    strncpy(d.deviceId, c.deviceId, sizeof(d.deviceId) - 1);
    d.panelDesc = panelDesc;
    snprintf(d.resLine, sizeof(d.resLine), "%d x %d", panelW, panelH);

    strncpy(d.urlPrimary, c.settingsUrl, sizeof(d.urlPrimary) - 1);
    if (c.lastIp[0])
        snprintf(d.urlSecondary, sizeof(d.urlSecondary), "Or http://%s", c.lastIp);

    // ---- next-refresh value
    if (state == ScreenState::Onboarding)
        d.nextValue[0] = '\0';
    else
        strncpy(d.nextValue, c.nextBase, sizeof(d.nextValue) - 1);

    // ---- per-state zone content
    switch (state) {
        case ScreenState::Normal:
            d.nextLabel = "Next image refresh";
            d.wifiHeading = c.wifiSsid[0] ? c.wifiSsid : "Wi-Fi";
            d.actionHeading = "Open settings";
            d.actionLine1 = "Scan with your phone.";
            d.actionLine2 = "Connect to the same Wi-Fi.";
            d.legend[0] = "Status";
            d.legend[1] = "Refresh image";
            d.legend[2] = "Pin image";
            snprintf(d.qrPayload, sizeof(d.qrPayload), "%s", c.settingsUrl);
            break;

        case ScreenState::Onboarding:
            d.nextLabel = "Setup";
            d.nextValue[0] = '\0';
            strncpy(d.nextValue, "Not connected yet", sizeof(d.nextValue) - 1);
            d.wifiHeading = "Not connected";
            d.actionHeading = "Join the setup network";
            d.actionLine1 = "Scan, or join the Wi-Fi below.";
            d.actionLine2 = "Then open http://192.168.4.1";
            snprintf(d.urlPrimary, sizeof(d.urlPrimary), "%s", apName);
            d.urlSecondary[0] = '\0';
            d.legend[0] = "Status";
            d.legend[1] = "";
            d.legend[2] = "";
            snprintf(d.qrPayload, sizeof(d.qrPayload), "WIFI:S:%s;;", apName);
            break;

        case ScreenState::Error:
            d.nextLabel = "Next image refresh";
            d.legend[0] = "Status";
            d.legend[1] = "Retry";
            d.legend[2] = "Pin image";
            snprintf(d.qrPayload, sizeof(d.qrPayload), "%s", c.settingsUrl);
            if (c.errorKind == ErrorKind::Image) {
                // The network is up; the image source is the problem. Keep
                // the real SSID + signal so the user isn't sent chasing Wi-Fi.
                d.wifiHeading = c.wifiSsid[0] ? c.wifiSsid : "Wi-Fi";
                d.actionHeading = "Image source problem";
                d.actionLine1 = c.errorMsg[0] ? c.errorMsg : "Couldn't load the image.";
                d.actionLine2 = "Retry, or check the URL in settings.";
            } else {
                d.wifiHeading = "Connection failed";
                d.actionHeading = "Wi-Fi problem";
                d.actionLine1 = c.errorMsg[0] ? c.errorMsg : "Couldn't reach the network.";
                d.actionLine2 = "Check your network, then Retry.";
            }
            break;
    }

    return d;
}
