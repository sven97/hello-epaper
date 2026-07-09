# Button Functions Implementation Plan

> **For agentic workers:** Single-task plan. Implement in one pass on the `button-functions` branch; verification is build-only for the implementer (hardware flash + checks are coordinated by the controller with the user).

**Goal:** Give the three user buttons distinct functions — refresh = new picture (GPIO3), toggle footer visibility (GPIO2), pin/freeze the current picture (GPIO5) — backed by a flash-saved copy of the current frame so non-fetch redraws need no network.

**Architecture:** The dithered 4 bpp frame (960,000 bytes, the panel's native 1600×1200 sprite buffer) is written to LittleFS after every successful fetch, *before* the footer is drawn — so any later redraw can reload the clean picture and stamp a fresh footer (or none). Two persistent booleans (`footerVisible`, `held`) plus the fetch metadata the footer needs on non-fetch wakes (fetch epoch, wifi description) live in NVS via `Preferences`. `setup()` dispatches on wake reason.

**Tech Stack:** Existing firmware + `LittleFS.h` (bundled with arduino-esp32; the `default_16MB.csv` partition table already has a ~6.9 MB `spiffs` partition that LittleFS mounts) and `Preferences.h` (NVS).

## Global Constraints (inherited)

- Repo `/Users/sven/Developer/ee02-playground`, work on branch `button-functions`. PlatformIO CLI only; `pio run` must succeed; do NOT flash (the board sleeps; the controller handles flashing).
- Panel: 1200×1600 logical (rotated), native sprite 1600×1200 at 4 bpp; palette nibbles; full refresh ~20–30 s blocks.
- Buttons GPIO2 (prev), GPIO3 (refresh), GPIO5 (next), active-LOW; LED GPIO21 active-LOW; deep-sleep pin holds on GPIO43/6 must be preserved on every sleep path.
- RTC system time survives deep sleep (not reset); NTP re-sync only on fetch wakes.
- Never edit library files or platformio.ini build flags.

## Behavior spec (authoritative)

| Wake | Action |
|---|---|
| `power-on` or `btn-refresh` | Fetch new photo → dither → **save frame to LittleFS** → footer if visible → update → sleep. Held state is KEPT (fetching while held pins the new photo). |
| `timer`, not held | Same as fetch flow above. |
| `timer`, held | Skip everything: no panel init, no pin-hold release, no fetch — re-arm sleep immediately (`quickSleep()`). |
| `btn-prev` | Toggle `footerVisible` (persist) → load frame from LittleFS → footer if now visible → update → sleep. If no saved frame exists (fresh filesystem), fall back to the fetch flow. |
| `btn-next` | Toggle `held` (persist) → LED feedback (2 blinks = now held, 1 blink = released) → if footer visible: load frame + footer + update (footer shows/loses the held marker); if footer hidden: no redraw → sleep. |
| Wi-Fi/fetch failure on a fetch path | `showError(...)` as today → sleep. |

Footer on non-fetch wakes: `last:` = stored fetch epoch (NOT now), `wifi:` = stored description from fetch time, `batt:` = live reading, `next:` = now + 1 h, or the literal `held` when pinned. When held and footer visible, `next: held` is the held marker.

## Files

- Modify: `src/main.cpp` only.

## Steps

- [ ] **Step 1: New includes and globals**

After the existing includes add:

```cpp
#include <LittleFS.h>
#include <Preferences.h>
```

Near the other globals add:

```cpp
Preferences prefs;                       // NVS namespace "frame"
const char *FRAME_PATH = "/frame.bin";
// Native sprite buffer: 1600*1200 px at 4 bpp
constexpr size_t FRAME_BYTES = 1600UL * 1200UL / 2;

bool footerVisible = true;
bool held = false;
```

- [ ] **Step 2: Frame save/load via the sprite buffer**

First check `.pio/libdeps/ee02/Seeed_GFX/Extensions/Sprite.h` for the public accessor to the sprite's backing buffer (stock TFT_eSPI names it `frameBuffer(int8_t)`; Seeed_GFX is a fork and it exists — verify the exact signature). Then add:

```cpp
// The sprite's raw 4 bpp buffer IS the display state (post-rotation), so a
// byte dump of it round-trips the picture exactly. Saved BEFORE the footer
// is drawn, so redraws can choose footer-on or footer-off.
bool saveFrame() {
    File f = LittleFS.open(FRAME_PATH, "w");
    if (!f) { Serial.println("frame save: open failed"); return false; }
    uint8_t *buf = (uint8_t *)epaper.frameBuffer(1);
    size_t written = f.write(buf, FRAME_BYTES);
    f.close();
    Serial.printf("frame save: %u bytes\n", (unsigned)written);
    return written == FRAME_BYTES;
}

bool loadFrame() {
    File f = LittleFS.open(FRAME_PATH, "r");
    if (!f || f.size() != FRAME_BYTES) {
        Serial.println("frame load: missing or wrong size");
        if (f) f.close();
        return false;
    }
    uint8_t *buf = (uint8_t *)epaper.frameBuffer(1);
    size_t got = f.read(buf, FRAME_BYTES);
    f.close();
    Serial.printf("frame load: %u bytes\n", (unsigned)got);
    return got == FRAME_BYTES;
}
```

If `frameBuffer` does not exist in this fork, fall back: keep a module-level `uint8_t *shadow = (uint8_t *)ps_malloc(FRAME_BYTES)` nibble copy maintained inside `ditherToPanel` (write each nibble to both the sprite and the shadow) and save/restore the shadow, restoring via a drawPixel loop. Note which path you took in your report.

- [ ] **Step 3: Footer rework**

Replace `drawStatusFooter`'s time/wifi sourcing so it works on non-fetch wakes:

```cpp
// Persist what the footer needs on non-fetch wakes. Must run on EVERY
// successful fetch, whether or not the footer is drawn — otherwise a
// hidden-footer fetch leaves stale metadata for the next footer-on redraw.
void recordFetchMetadata() {
    prefs.putULong("lastEpoch", (uint32_t)time(nullptr));
    prefs.putString("wifiDesc",
                    WiFi.SSID() + " " + String(WiFi.RSSI()) + "dBm");
}

void drawStatusFooter(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    String status = "wake: " + String(wakeReason()) + "  |  ";

    // Metadata is recorded by recordFetchMetadata() on EVERY successful
    // fetch (footer visible or not); the footer only ever reads it.
    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String wifiDesc = prefs.getString("wifiDesc", "?");

    if (lastEpoch > 1600000000) { // sanity: clock was ever synced
        struct tm lastTm;
        localtime_r(&lastEpoch, &lastTm);
        char dow[16], hm[8];
        strftime(dow, sizeof(dow), "%a %b", &lastTm);
        strftime(hm, sizeof(hm), "%H:%M", &lastTm);
        status += "last: " + String(dow) + " " + String(lastTm.tm_mday) +
                  " " + String(hm);
    } else {
        status += "last: --";
    }

    if (held) {
        status += "  |  next: held";
    } else {
        time_t next = time(nullptr) + (time_t)SLEEP_SECONDS;
        struct tm nextTm;
        localtime_r(&next, &nextTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &nextTm);
        status += "  |  next: " + String(hm);
    }

    status += "  |  wifi: " + wifiDesc;
    status += "  |  batt: " + String(batteryPercent(vbatMv)) + "% (" +
              String(vbatMv / 1000.0f, 2) + "V";
    if (haveDelta) {
        status += ", ";
        if (deltaMv >= 0) status += "+";
        status += String(deltaMv) + "mV";
        if (deltaMv >= 20) status += ", chg";
    }
    status += ")";
    epaper.fillRect(0, 1560, 1200, 40, TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString(status, 20, 1568, 2);
}
```

`syncClock()` stays as-is but is only called on fetch wakes.

- [ ] **Step 4: quickSleep for the held timer path**

```cpp
// Timer wake while held: nothing to draw, panel was never woken and the
// GPIO holds from the previous sleep are still latched — re-arm and go.
void quickSleep() {
    Serial.printf("held — back to sleep %llu s\n", SLEEP_SECONDS);
    Serial.flush();
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}
```

- [ ] **Step 5: LED feedback helper**

```cpp
void blinkLed(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(150);
        digitalWrite(LED_PIN, HIGH);
        delay(150);
    }
}
```

- [ ] **Step 6: setup() dispatch**

Restructure `setup()` to this flow (keep the existing battery/delta block, hold-release block, and forget-wifi gesture exactly where they are; the new logic replaces the tail):

```cpp
void setup() {
    Serial.begin(115200);
    delay(2000);
    bootCount++;
    Serial.printf("ee02-playground — boot #%u, wake: %s\n",
                  bootCount, wakeReason());

    prefs.begin("frame", false);
    footerVisible = prefs.getBool("footer", true);
    held = prefs.getBool("held", false);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t btnBits = (cause == ESP_SLEEP_WAKEUP_EXT1)
                           ? esp_sleep_get_ext1_wakeup_status() : 0;

    // Held timer wake: skip everything, don't even touch the panel.
    if (cause == ESP_SLEEP_WAKEUP_TIMER && held) quickSleep(); // no return

    // (existing) release pin holds, LED on, battery read + delta block ...

    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

    epaper.begin();

    // (existing) forget-wifi gesture on refresh held at boot ...

    bool isToggleFooter = btnBits & (1ULL << BTN_PREV);
    bool isToggleHold   = btnBits & (1ULL << BTN_NEXT);

    if (isToggleFooter || isToggleHold) {
        if (isToggleFooter) {
            footerVisible = !footerVisible;
            prefs.putBool("footer", footerVisible);
            Serial.printf("footer now %s\n", footerVisible ? "on" : "off");
        } else {
            held = !held;
            prefs.putBool("held", held);
            Serial.printf("held now %s\n", held ? "on" : "off");
            blinkLed(held ? 2 : 1);
        }
        bool needRedraw = isToggleFooter || footerVisible;
        if (needRedraw && loadFrame()) {
            if (footerVisible)
                drawStatusFooter(vbatMv, deltaMv, haveDelta);
            Serial.println("updating panel (takes ~20-30 s)...");
            epaper.update();
            Serial.println("done");
        } else if (needRedraw) {
            // No saved frame yet — fall through to a full fetch instead.
            goto fetch;
        }
    } else {
fetch:
        if (!connectWifi()) {
            showError("wifi setup failed or timed out");
        } else if (fetchImage()) {
            syncClock();
            saveFrame();
            recordFetchMetadata();
            if (footerVisible)
                drawStatusFooter(vbatMv, deltaMv, haveDelta);
            Serial.println("updating panel (takes ~20-30 s)...");
            epaper.update();
            Serial.println("done");
        }
    }

    digitalWrite(LED_PIN, HIGH);
    goToSleep();
}
```

`goto` into a scoped block is illegal in C++ if it jumps over initializations — restructure into a small helper instead of a literal `goto` if the compiler objects (e.g. `void doFetchCycle(...)` called from both places). Prefer the helper; the snippet above conveys intent, not literal text. All variables the footer needs (`vbatMv`, `deltaMv`, `haveDelta`) come from the existing battery block.

- [ ] **Step 7: Build**

```bash
pio run
```

Expected `SUCCESS`. Do NOT upload.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp docs/superpowers/plans/2026-07-09-button-functions.md
git commit -m "Button functions: refresh / footer toggle / pin-freeze, frame persisted to LittleFS"
```

## Hardware verification checklist (controller + user, post-flash)

1. Flash → `wake: power-on`, new photo, footer visible, serial shows `frame save: 960000 bytes`.
2. Press **prev** → same photo redrawn without footer (serial: `footer now off`, `frame load: 960000 bytes`, no wifi lines).
3. Press **prev** again → same photo, footer back, `last:` shows the original fetch time (not now).
4. Press **next** → 2 LED blinks; footer redraw shows `next: held`.
5. Wait for a timer wake (or shorten SLEEP_SECONDS temporarily) → serial `held — back to sleep`, photo unchanged.
6. Press **refresh** → new photo, still `next: held`.
7. Press **next** → 1 blink, footer shows a real `next:` time again.
