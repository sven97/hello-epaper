# UI/UX Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the UX problems found in the 2026-07-11 UI walkthrough: unattended failures destroy the photo, the provisioning/error screens use tiny top-left text, status-page wording is developer jargon, the portal's done pages are dead ends, the portal dies between KEY1 sessions in dev mode, and the LED can't tell "working" from "hung."

**Architecture:** No new modules. Behavior changes concentrate in `main.cpp` (interactive vs unattended fetch), `net.cpp` (provisioning screen v2, portal-less unattended connects), `ui.cpp`/`display.cpp` (wording + shared QR helper), `portal.*` (copy + non-blocking dev-mode service), `power.*` (LED task). Screenshots that motivated each change: scratchpad `portal-*.png`, `eink-*.png` from this session.

**Tech Stack:** Existing: Arduino/ESP32-S3, Seeed_GFX, WiFiManager, ricmoo/QRCode, FreeRTOS (part of the Arduino core).

## Global Constraints

- Both `pio run -e ee02` and `pio test -e native` must pass before every commit (29 native test cases today; none of this plan's code is natively testable — the suite guards regressions).
- Commit style: imperative summary, no conventional-commit prefixes. Never edit files under `.pio/`.
- All panel coordinates must render in every rotation: derive from `epaper.width()/height()`, or keep absolute y-values ≤ 1150 (min panel height is 1200 in landscape).
- Panel font: GFX font 4 glyph height is 26 px at `setTextSize(1)`, 52 px at `setTextSize(2)`. Every drawString in this plan names its size; always restore `setTextSize(1)` and `TL_DATUM` before returning.
- User-facing copy rules: no dev jargon (`btn-info`, `mV`, lowercase `wifi` as a sentence-starter); every error screen tells the user what to do next.
- QR codes: version 4, ECC_LOW, 4-module quiet zone (the shared helper enforces this); payload must stay ≤ 78 bytes.
- NVS keys and their meanings are frozen — this plan adds none.

---

### Task 1: Unattended failures keep the photo; error screen redesigned

**Files:**
- Modify: `src/net.h`, `src/net.cpp` (`connectWifi` gains `allowPortal`; `fetchImage` stops drawing)
- Modify: `src/main.cpp` (`doFetchCycle(bool interactive)`)
- Modify: `src/display.cpp` (`showError` v2)
- Modify: `README.md`, `docs/hardware-checklist.md`

**Interfaces:**
- Consumes: existing `showError(const String&)` (display.h), `plannedSleepSecs()` (power.h).
- Produces: `bool connectWifi(bool allowPortal = true);` (net.h) — `false` arg means: never open the provisioning AP, never draw the provisioning screen, fail fast. `bool fetchImage(String &err);` — on failure fills `err` with user-facing copy and draws NOTHING. `static void doFetchCycle(bool interactive)` in main.cpp — Tasks 5 uses the same signature.

- [ ] **Step 1: `src/net.h` — new signatures.** Replace the `connectWifi`/`fetchImage` declarations:

```cpp
// Connect with saved credentials. With allowPortal (the default), first
// boot / stale credentials open the captive portal (AP_NAME) and the
// panel shows instructions. With allowPortal=false — unattended timer
// wakes — never touch the panel or open an AP: fail fast and return
// false so the caller can keep the current photo and retry next wake.
bool connectWifi(bool allowPortal = true);

// Fetch a photo into the sprite, dithered. On failure fills err with a
// short user-facing message and draws nothing — the caller decides
// whether anyone is watching.
bool fetchImage(String &err);
```

- [ ] **Step 2: `src/net.cpp` — implement both.** In `connectWifi`, gate the portal paths:

```cpp
bool connectWifi(bool allowPortal) {
    provisioningScreenShown = false; // each attempt may open a fresh portal
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
```

In `fetchImage`, change the signature to `bool fetchImage(String &err)` and replace every `showError("...")` + `return false` pair with `err = "..."` + `return false` (no drawing). New copy, in order of the existing failure branches:
  - HTTP status: `err = "image server said HTTP " + String(code);`
  - no length: `err = "image server sent no size";`
  - alloc fail: `err = "out of memory for the image";`
  - incomplete: `err = "image download was cut off";`
  - decode fail: `err = "that URL is not a baseline JPEG";`

- [ ] **Step 3: `src/main.cpp` — interactive vs unattended.** Replace `doFetchCycle` with:

```cpp
// Fetch a new photo, dither it, persist metadata, and show it full-bleed.
// interactive=false (scheduled timer wakes): nobody is watching — on any
// failure keep the current photo untouched, log, and let the next wake
// retry. interactive=true (button presses, power-on, portal exits): draw
// the error screen so the person standing there knows what happened.
static void doFetchCycle(bool interactive) {
    if (!connectWifi(interactive)) {
        if (interactive) showError("Wi-Fi connection failed");
        else Serial.println("wifi failed — keeping photo, retry next wake");
        return;
    }
    String err;
    if (!fetchImage(err)) {
        if (interactive) showError(err);
        else Serial.println("fetch failed (" + err + ") — keeping photo");
        return;
    }
    syncClock();
    recordFetchMetadata();
    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done");
}
```

Update every call site:
  - `setup()` dispatch: `runStatusMode` branch → `doFetchCycle(true)`; the final `else` branch → `doFetchCycle(cause != ESP_SLEEP_WAKEUP_TIMER);` (power-on and KEY2 are interactive, the hourly timer is not). The existing `else showError("wifi setup failed or timed out");` after `runStatusMode` becomes `else showError("Wi-Fi connection failed");`.
  - `loop()`: the `info` branch and `newPic` path → `doFetchCycle(true)`; `fetchDue` alone → `doFetchCycle(false)`. Restructure the tail of `loop()`'s action block:

```cpp
        if (info) {
            if (runStatusMode(vbatMv, deltaMv, haveDelta)) doFetchCycle(true);
            else showError("Wi-Fi connection failed");
        } else {
            doFetchCycle(newPic); // KEY2 is interactive; fetchDue is not
        }
```

- [ ] **Step 4: `src/display.cpp` — error screen v2.** Replace `showError`:

```cpp
// Full-panel error screen (calls update()). Only drawn when someone is
// watching (button-initiated actions) — unattended wakes keep the photo.
void showError(const String &msg) {
    const int cx = epaper.width() / 2, cy = epaper.height() / 2;
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextSize(2);
    epaper.setTextColor(TFT_RED, TFT_WHITE);
    epaper.drawString("Something went wrong", cx, cy - 100, 4);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString(msg, cx, cy, 4);
    epaper.setTextSize(1);
    epaper.drawString("Check your Wi-Fi, then press KEY2 to try again.",
                      cx, cy + 90, 4);
    epaper.setTextDatum(TL_DATUM);
    epaper.update();
}
```

- [ ] **Step 5: Docs.** README "Settings" section, after the portal-stops sentence, add: `Transient failures on scheduled refreshes never touch the panel — the current photo stays up and the next wake retries.` In `docs/hardware-checklist.md`, add under "Settings behavior": `- [ ] Unplug the router, wait for a scheduled refresh: photo stays, serial logs "keeping photo"; press KEY2: centered error screen appears`.

- [ ] **Step 6: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: SUCCESS and 29/29.

- [ ] **Step 7: Commit**

```bash
git add src/net.h src/net.cpp src/main.cpp src/display.cpp README.md docs/hardware-checklist.md
git commit -m "Keep the photo on unattended fetch failures; redesign the error screen"
```

---

### Task 2: Shared QR helper + provisioning screen v2 (QRs, big text)

**Files:**
- Modify: `src/display.h`, `src/display.cpp` (shared `drawQrCode`)
- Modify: `src/ui.cpp` (drop its private `drawQr`, use the shared one)
- Modify: `src/net.cpp` (`showProvisioningScreen` v2)
- Modify: `README.md`, `docs/hardware-checklist.md`

**Interfaces:**
- Consumes: `qrcode.h` (ricmoo/QRCode, already a dependency).
- Produces: `void drawQrCode(const String &text, int cx, int cy, int scale);` in display.h — version-4 (33×33) QR centered at (cx,cy), 4-module white quiet zone, draws into the sprite only. Tasks 3 uses it from ui.cpp.

- [ ] **Step 1: Move the QR renderer.** In `src/display.h`, after `applyOrientation()`:

```cpp
// Version-4 (33x33) QR centered at (cx, cy), scale px per module, with a
// 4-module white quiet zone. Draws into the sprite only. Payload must fit
// version 4 at ECC_LOW (78 bytes).
void drawQrCode(const String &text, int cx, int cy, int scale);
```

In `src/display.cpp`, add `#include <qrcode.h>` and the function — this is `ui.cpp`'s current `drawQr` verbatim, renamed:

```cpp
void drawQrCode(const String &text, int cx, int cy, int scale) {
    QRCode qr;
    uint8_t data[qrcode_getBufferSize(4)];
    if (qrcode_initText(&qr, data, 4, ECC_LOW, text.c_str()) != 0) return;
    const int px = qr.size * scale;
    const int x0 = cx - px / 2, y0 = cy - px / 2;
    epaper.fillRect(x0 - 4 * scale, y0 - 4 * scale, px + 8 * scale,
                    px + 8 * scale, TFT_WHITE);
    for (int y = 0; y < qr.size; y++)
        for (int x = 0; x < qr.size; x++)
            if (qrcode_getModule(&qr, x, y))
                epaper.fillRect(x0 + x * scale, y0 + y * scale, scale, scale,
                                TFT_BLACK);
}
```

In `src/ui.cpp`: delete the static `drawQr` and its comment, delete `#include <qrcode.h>`, and change the one call `drawQr(url, cx, qrCy, scale)` → `drawQrCode(url, cx, qrCy, scale)`.

- [ ] **Step 2: Provisioning screen v2.** In `src/net.cpp`, replace `showProvisioningScreen` (and delete the now-wrong "Layout note" comment above it):

```cpp
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
```

(Layout check, both rotations: QRs at scale 4 span 132 px + 32 px quiet zone → [302, 498] and [782, 978]; nearest text rows at 545/725/1060 clear them; max y 1140 < 1200.)

- [ ] **Step 3: Docs.** README "First-time setup" step 2 becomes: `2. The panel shows two QR codes: scan the first to join the frame's hotspot (EE02-Setup), and the setup page opens by itself (second QR / http://192.168.4.1 as fallback). Pick your 2.4 GHz network.` Hardware checklist, under "Regressions", extend the cold-boot line: `- [ ] Cold boot with no Wi-Fi saved: provisioning screen with two scannable QR codes; hotspot QR joins, portal QR opens the page`.

- [ ] **Step 4: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: SUCCESS and 29/29.

- [ ] **Step 5: Commit**

```bash
git add src/display.h src/display.cpp src/ui.cpp src/net.cpp README.md docs/hardware-checklist.md
git commit -m "Provisioning screen v2: join-hotspot and portal QR codes, readable text sizes"
```

---

### Task 3: Status page wording + QR caption grouping

**Files:**
- Modify: `src/ui.h`, `src/ui.cpp`

**Interfaces:**
- Consumes: `drawQrCode` (Task 2), `plannedSleepSecs()` (power.h).
- Produces: `const char *wakeReasonHuman();` in ui.h. `wakeReason()` keeps its dev-facing strings for serial logs — do not change it.

- [ ] **Step 1: `src/ui.h`** — add below `wakeReason()`:

```cpp
// Same dispatch as wakeReason(), user-facing words — for the panel.
const char *wakeReasonHuman();
```

- [ ] **Step 2: `src/ui.cpp`** — add the function after `wakeReason()`:

```cpp
const char *wakeReasonHuman() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "scheduled refresh";
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t bits = esp_sleep_get_ext1_wakeup_status();
            if (bits & (1ULL << BTN_INFO))    return "KEY1 (settings)";
            if (bits & (1ULL << BTN_NEW_PIC)) return "KEY2 (new picture)";
            if (bits & (1ULL << BTN_PIN))     return "KEY3 (pin)";
            return "button";
        }
        default: return "power on";
    }
}
```

- [ ] **Step 3: Rewrite the info lines in `drawStatusScreen`.** Replace the `lines[...]` construction (keep the metadata reads above it):

```cpp
    String lines[5];
    int n = 0;
    lines[n++] = settings.name + "  —  woke by " + wakeReasonHuman();

    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm lastTm;
        localtime_r(&lastEpoch, &lastTm);
        char dow[16], hm[8];
        strftime(dow, sizeof(dow), "%a %b", &lastTm);
        strftime(hm, sizeof(hm), "%H:%M", &lastTm);
        lines[n++] = "last photo: " + String(dow) + " " +
                     String(lastTm.tm_mday) + " " + String(hm);
    } else {
        lines[n++] = "last photo: --";
    }

    if (held) {
        lines[n++] = "next photo: pinned (KEY3 resumes)";
    } else {
        time_t next = time(nullptr) + (time_t)plannedSleepSecs();
        struct tm nextTm;
        localtime_r(&next, &nextTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &nextTm);
        lines[n++] = "next photo: " + String(hm);
    }

    lines[n++] = "Wi-Fi: " + wifiDesc;

    String batt = "battery: " + String(batteryPercent(vbatMv)) + "% (" +
                  String(vbatMv / 1000.0f, 2) + " V";
    if (haveDelta && deltaMv >= 20) batt += ", charging";
    batt += ")";
    lines[n++] = batt;
```

(The wake-to-wake mV delta stays in the serial log only — it's telemetry, not UI.)

- [ ] **Step 4: Group the QR with a caption.** Replace everything between the info-line loop and the legend (the current "Portal URL at base text size" block through the `drawQr` call) with:

```cpp
    // Caption + URL directly above the QR so the three read as one unit.
    String url = portalUrl();
    epaper.drawString("Scan to open settings", cx, y, 4); // still size 2
    epaper.setTextSize(1);
    epaper.drawString(url + (lastIp.isEmpty() ? "" : "   (" + lastIp + ")"),
                      cx, y + 55, 4);

    // Center the QR in the free band below the URL line, scaled so its
    // full extent (33 modules + 4-module quiet zone each side) fits.
    const int legendTop = epaper.height() - 200;
    const int bandTop = y + 85;
    const int bandBottom = legendTop - 30;
    const int qrCy = (bandTop + bandBottom) / 2;
    const int scale = min(8, (bandBottom - bandTop) / (33 + 8));
    drawQrCode(url, cx, qrCy, scale);
```

(Layout check — portrait: lines end y=730, caption 730, URL 785, band [815, 1370], scale 8, QR dark modules [960, 1224], legend at 1400. Landscape: lines end y=630, caption 630, URL 685, band [715, 970], scale 6, dark modules [743, 941], legend at 1000 — ≥46 px clear everywhere.)

- [ ] **Step 5: Legend wording.** Replace the three legend drawStrings:

```cpp
    epaper.drawString("KEY1: back to photo — closes settings", cx, legendTop, 4);
    epaper.drawString("KEY2: new picture", cx, legendTop + 55, 4);
    epaper.drawString(held ? "KEY3: unpin — refreshes resume"
                           : "KEY3: pin this picture",
                      cx, legendTop + 110, 4);
```

- [ ] **Step 6: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: SUCCESS and 29/29.

- [ ] **Step 7: Commit**

```bash
git add src/ui.h src/ui.cpp
git commit -m "Status page: human wording, charging indicator, QR grouped under a caption"
```

---

### Task 4: Portal copy polish (done pages, padded hours, status line)

**Files:**
- Modify: `src/portal_html.h` (`%STATUS%` token, done-page style)
- Modify: `src/portal.cpp` (status line, padded hour labels, done-page copy)
- Modify: `src/state.h`, `src/main.cpp` (expose `lastVbatMv`)

**Interfaces:**
- Consumes: `batteryPercent`, `plannedSleepSecs` (power.h — already included in portal.cpp), `CLOCK_SANE_EPOCH` (config.h).
- Produces: `extern int32_t lastVbatMv;` in state.h (defined in main.cpp — remove `RTC_DATA_ATTR`? NO: keep the definition exactly as is; `RTC_DATA_ATTR int32_t lastVbatMv` already has external linkage).

- [ ] **Step 1: Expose the battery reading.** In `src/state.h` add below `extern bool held;`:

```cpp
extern int32_t lastVbatMv; // most recent battery read (RTC-persisted, main.cpp)
```

(`main.cpp`'s `RTC_DATA_ATTR int32_t lastVbatMv = -1;` already has external linkage — no change there.)

- [ ] **Step 2: `src/portal_html.h`** — insert a status line after the `<h1>`: change `<h1>%NAME% — settings</h1>` to:

```html
<h1>%NAME% — settings</h1>
<p class="note" style="margin-top:-.6rem">%STATUS%</p>
```

And give `PORTAL_DONE_HTML` the same body styling plus a note style — replace its `<style>` line with:

```html
<style>body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
.note{color:#666;font-size:.85rem}</style>
```

Also change `PORTAL_DONE_HTML`'s `<p>%BODY%</p>` to `<div>%BODY%</div>` — Task 4's bodies contain `<p class="note">` blocks, and `p` cannot nest inside `p`.

- [ ] **Step 3: `src/portal.cpp`** — add `#include <time.h>`, then a status-line builder above `buildPage`:

```cpp
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
```

In `buildPage`, add `page.replace("%STATUS%", statusLine());` next to the `%NAME%` replacement. In `selectOptions`, zero-pad the label (values unchanged):

```cpp
        String label = (v < 10 ? "0" : "") + String(v) + suffix;
        out += "<option value=\"" + String(v) + "\"" +
               (v == selected ? " selected" : "") + ">" + label + "</option>";
```

- [ ] **Step 4: Done-page copy — no more dead ends.** In the three handlers, update the `sendDone` bodies:
  - `handleSave`: `"The frame is applying settings and fetching a picture — the panel takes ~30 s to refresh.<p class=\"note\">To open settings again later, press KEY1 on the frame.</p>"`
  - `handleNewPic`: `"New picture on the way — the panel takes ~30 s to refresh.<p class=\"note\">To open settings again later, press KEY1 on the frame.</p>"`
  - `handleForgetWifi`: `"The frame will show setup instructions on its screen. Join the <b>" + String(AP_NAME) + "</b> hotspot to reconnect."`

- [ ] **Step 5: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: SUCCESS and 29/29.

- [ ] **Step 6: Commit**

```bash
git add src/portal_html.h src/portal.cpp src/state.h
git commit -m "Portal polish: device status line, padded hours, done pages that say what's next"
```

---

### Task 5: Dev-mode portal stays up

**Files:**
- Modify: `src/portal.h`, `src/portal.cpp` (non-blocking service + persistent mode)
- Modify: `src/main.cpp` (`loop()` wiring)
- Modify: `README.md`, `docs/hardware-checklist.md`

**Interfaces:**
- Consumes: `startPortal()`, `PortalResult`, `doFetchCycle(bool)` (Task 1), `applyOrientation()`, `applyUtcOffset(long)`.
- Produces: in portal.h — `void setPortalPersistent(bool on);` (persistent portals survive `runPortal`'s exit: server + mDNS keep running), `void servicePortal();` (pump `handleClient`; safe no-op when not started), `bool takePortalAction();` (true exactly once after a handler requested apply/exit). `startPortal()` becomes idempotent.

- [ ] **Step 1: `src/portal.h`** — add below `runPortal`:

```cpp
// Dev mode (USB host attached, device never sleeps): the portal runs
// permanently. setPortalPersistent(true) makes runPortal() leave the
// server + mDNS up on exit; servicePortal() pumps requests from loop();
// takePortalAction() reports (once) that a handler asked to apply
// settings — the caller reapplies orientation/TZ and fetches.
void setPortalPersistent(bool on);
void servicePortal();
bool takePortalAction();
```

- [ ] **Step 2: `src/portal.cpp`** — module state and implementations. Add next to the existing statics:

```cpp
static bool portalRunning;
static bool portalPersistent;
```

Make `startPortal` idempotent and record state — add at its top:

```cpp
    if (portalRunning) return true;
```

and before its `return true;`:

```cpp
    portalRunning = true;
```

In `runPortal`, replace the unconditional teardown (`delay(200); server.stop(); MDNS.end();`) with:

```cpp
    delay(200); // let the last HTTP response flush
    exitRequested = false; // consumed by this session — takePortalAction()
                           // must not re-fire on it after runPortal returns
    if (!portalPersistent) {
        server.stop();
        MDNS.end();
        portalRunning = false;
    }
```

Add the three new functions at the end of the file:

```cpp
void setPortalPersistent(bool on) { portalPersistent = on; }

void servicePortal() {
    if (portalRunning) server.handleClient();
}

bool takePortalAction() {
    if (!exitRequested) return false;
    exitRequested = false;
    return true;
}
```

- [ ] **Step 3: `src/main.cpp` — wire into `loop()`.** After the `usbHostPresent()` gate at the top of `loop()`, insert:

```cpp
    // Dev mode: keep the settings portal up permanently — the device
    // never sleeps while a USB host is attached, so it costs nothing.
    static bool devPortalStarted = false;
    if (!devPortalStarted && WiFi.status() == WL_CONNECTED) {
        setPortalPersistent(true);
        devPortalStarted = startPortal();
        if (devPortalStarted)
            Serial.println("dev mode: portal up at " + portalUrl());
    }
    servicePortal();
    if (takePortalAction()) {
        applyUtcOffset(prefs.getLong("tzOff", 0));
        applyOrientation();
        digitalWrite(LED_PIN, LOW);
        doFetchCycle(true);
        digitalWrite(LED_PIN, HIGH);
    }
```

(KEY1 in dev mode still runs `runStatusMode` → `startPortal()` is now a no-op because the portal is already running, and `runPortal`'s teardown is skipped because persistent is set — the session behaves the same, the portal just survives it.)

- [ ] **Step 4: Docs.** README "Dev mode" paragraph, append: `While plugged in, the settings portal stays reachable at http://<name>.local the whole time — no KEY1 needed.` Hardware checklist, under "Regressions", extend the dev-mode line: `..., portal reachable without pressing KEY1, saving from it fetches a new photo`.

- [ ] **Step 5: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: SUCCESS and 29/29.

- [ ] **Step 6: Commit**

```bash
git add src/portal.h src/portal.cpp src/main.cpp README.md docs/hardware-checklist.md
git commit -m "Dev mode: settings portal stays up permanently while a USB host is attached"
```

---

### Task 6: LED heartbeat while working

**Files:**
- Modify: `src/power.h`, `src/power.cpp` (LED task + modes)
- Modify: `src/main.cpp` (replace raw LED writes)

**Interfaces:**
- Consumes: `LED_PIN` (config.h, active-LOW).
- Produces: in power.h — `enum class LedMode : uint8_t { Off, Solid, Heartbeat };`, `void startLedTask();`, `void setLed(LedMode mode);`. `blinkLed` keeps its signature but now suspends the task's control while it blinks.

- [ ] **Step 1: `src/power.h`** — add above `blinkLed`:

```cpp
// LED policy (active-LOW pin): Solid = awake and idle, Heartbeat = busy
// (fetching / dithering / refreshing the panel — proof it isn't hung),
// Off = about to sleep. A tiny FreeRTOS task owns the pin; ack blinks
// (blinkLed) temporarily take it over.
enum class LedMode : uint8_t { Off, Solid, Heartbeat };
void startLedTask(); // call once per boot before the first setLed()
void setLed(LedMode mode);
```

- [ ] **Step 2: `src/power.cpp`** — the task and mode plumbing:

```cpp
static volatile LedMode ledMode = LedMode::Off;

static void ledTask(void *) {
    for (;;) {
        switch (ledMode) {
            case LedMode::Off:
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LedMode::Solid:
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LedMode::Heartbeat:
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(pdMS_TO_TICKS(120));
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(pdMS_TO_TICKS(880));
                break;
        }
    }
}

void startLedTask() { xTaskCreate(ledTask, "led", 2048, nullptr, 1, nullptr); }

void setLed(LedMode m) { ledMode = m; }
```

Replace `blinkLed` so ack blinks don't fight the task for the pin:

```cpp
void blinkLed(int times, int onOffMs) {
    LedMode prev = ledMode;
    ledMode = LedMode::Off;
    delay(120); // let the task release the pin (its longest hold is 100 ms)
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(onOffMs);
        digitalWrite(LED_PIN, HIGH);
        delay(onOffMs);
    }
    ledMode = prev;
}
```

- [ ] **Step 3: `src/main.cpp`** — swap the raw writes for modes:
  - In `setup()` after the button `pinMode` block: replace `digitalWrite(LED_PIN, LOW); // LED on while awake` with `startLedTask();` followed by `setLed(LedMode::Solid);`.
  - In `doFetchCycle`, first line: `setLed(LedMode::Heartbeat);` and before every `return` and at the end: `setLed(LedMode::Solid);` (three failure returns + the success path — four call sites).
  - In `setup()` before `maybeSleep()`: replace `digitalWrite(LED_PIN, HIGH);` with `setLed(LedMode::Off);`.
  - In `loop()`'s action block (both in the Task 5 insert and the button block): replace `digitalWrite(LED_PIN, LOW);` → `setLed(LedMode::Solid);` and `digitalWrite(LED_PIN, HIGH);` → `setLed(LedMode::Off);`.
  - `runStatusMode` needs the panel-draw phases busy too: add `setLed(LedMode::Heartbeat);` before the `epaper.update()` call and `setLed(LedMode::Solid);` after it.

- [ ] **Step 4: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: SUCCESS and 29/29.

- [ ] **Step 5: Commit**

```bash
git add src/power.h src/power.cpp src/main.cpp
git commit -m "LED heartbeat while fetching or refreshing so busy never looks hung"
```

---

## Post-plan (owner)

- Run the updated `docs/hardware-checklist.md` on the frame — the QR scans and the heartbeat need eyes.
- The unattended-failure change means a dead image URL shows up as "last photo" going stale on the status page rather than an error screen; the portal status line (Task 4) makes that visible from the couch.
