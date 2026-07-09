# EE02 Playground Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A learning firmware for the Seeed XIAO ePaper Display Board EE02 (XIAO ESP32-S3 Plus + 13.3" Spectra 6 panel) built in four on-hardware milestones: display hello-world, buttons, Wi-Fi image fetch, battery + deep sleep.

**Architecture:** Single PlatformIO project, Arduino framework, one `src/main.cpp` that grows milestone by milestone. Seeed_GFX drives the panel (selected entirely via two build flags — no library file edits). Verification is on-hardware: every task ends with something observable on the panel or serial monitor.

**Tech Stack:** PlatformIO CLI, Arduino-ESP32, Seeed_GFX (Seeed-Studio/Seeed_GFX), JPEGDecoder (bodmer), WiFiManager (tzapu), ESP32 deep-sleep APIs.

## Global Constraints

- Work in `/Users/sven/Developer/ee02-playground` (git repo already exists with the spec committed).
- Panel is 1200×1600 native portrait, six colors: `TFT_BLACK`, `TFT_WHITE`, `TFT_RED`, `TFT_YELLOW`, `TFT_GREEN`, `TFT_BLUE`. A full Spectra 6 refresh takes ~20–30 seconds — this is normal, not a hang.
- Display selection is exactly these two compile-time defines (from Seeed's config tool): `BOARD_SCREEN_COMBO=510` and `USE_XIAO_EPAPER_DISPLAY_BOARD_EE02`. They go in `platformio.ini` `build_flags`, never edited into library files.
- PSRAM is required (framebuffer > internal RAM). The `seeed_xiao_esp32s3` board definition already sets `-DBOARD_HAS_PSRAM` and `memory_type: qio_opi` — do not remove it.
- Board pin facts (verified against the EE02 v1.0 schematic and working community firmware):
  - Buttons (active-LOW, external 10 kΩ pull-ups to 3V3): GPIO2 = "previous", GPIO3 = "refresh", GPIO5 = "next". All three are RTC-capable (valid EXT1 wake sources).
  - Battery: voltage on GPIO1 (A0) through a ÷2 divider; GPIO6 must be driven HIGH to enable the divider, LOW otherwise to save power.
  - ePaper power enable: GPIO43 (the library drives it during use; drive it LOW before deep sleep).
  - XIAO user LED: GPIO21, active-LOW.
- Serial is USB-CDC at 115200 (`ARDUINO_USB_CDC_ON_BOOT=1` comes from the board definition). After flashing, reopen the monitor with `pio device monitor`.
- Wi-Fi is 2.4 GHz only (ESP32-S3 has no 5 GHz).
- All builds/flashes via PlatformIO CLI: `pio run`, `pio run -t upload`, `pio device monitor`. Never the Arduino IDE.
- No credentials in the repo: Wi-Fi provisioning happens on-device via the WiFiManager captive portal (AP `EE02-Setup`, portal at `http://192.168.4.1`, network list with signal strength); credentials persist in NVS across boots and deep sleep. Holding the refresh button (GPIO3) at power-on clears them.
- Image source: random picsum.photos picture at exactly panel size, fetched through the images.weserv.nl proxy which re-encodes to **baseline** JPEG (`https://images.weserv.nl/?url=picsum.photos/1200/1600%3Frandom%3D<n>&output=jpg` with a random `<n>` per fetch to defeat the proxy cache). Direct picsum URLs serve *progressive* JPEGs, which embedded decoders (JPEGDecoder et al.) cannot parse — verified 2026-07-08. HTTPS via `WiFiClientSecure` with `setInsecure()` (no cert validation in a learning repo); redirects followed.
- No unit-test scaffolding. The test cycle per task is: build → flash → observe on panel/serial → commit.
- The 6-color framebuffer is 4 bpp **palette-indexed**: `TFT_*` color macros are panel nibbles (WHITE=0x0, GREEN=0x2, RED=0x6, YELLOW=0xB, BLUE=0xD, BLACK=0xF) and `drawPixel` stores `color & 0x0F` directly. RGB565 image data must never be pushed raw (`pushImage` renders garbage); photos are Floyd–Steinberg dithered to the 6-color palette and written pixel-by-pixel as nibbles.

**Troubleshooting facts (apply in any task):**
- If no serial port appears: hold the BOOT button while plugging in USB, then flash; press RESET after.
- Find the port with `pio device list` (expect `/dev/cu.usbmodem…`).
- If the panel stays blank but the build/flash succeeded, the two build-flag defines are wrong or missing — re-check `platformio.ini` before touching code.
- If the build fails inside Seeed_GFX with Arduino-core API errors, pin the newer core by replacing the `platform =` line with `platform = https://github.com/pioarduino/platform-espressif32/releases/download/54.03.20/platform-espressif32.zip` and rebuild.

---

### Task 1: Toolchain + project scaffold (serial hello)

**Files:**
- Create: `platformio.ini`
- Create: `.gitignore`
- Create: `src/main.cpp`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: a building, flashing PlatformIO project; `platformio.ini` env name `ee02`; `src/main.cpp` with `setup()`/`loop()` that later tasks replace wholesale.

- [ ] **Step 1: Install PlatformIO CLI (skip if `pio --version` already works)**

```bash
brew install platformio
pio --version
```

Expected: a version string like `PlatformIO Core, version 6.x.x`.

- [ ] **Step 2: Write `platformio.ini`**

```ini
[env:ee02]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200

; XIAO ESP32-S3 Plus has 16 MB flash (stock board def assumes 8 MB)
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv

lib_deps =
    https://github.com/Seeed-Studio/Seeed_GFX.git

build_flags =
    -DBOARD_SCREEN_COMBO=510
    -DUSE_XIAO_EPAPER_DISPLAY_BOARD_EE02
```

- [ ] **Step 3: Write `.gitignore`**

```gitignore
.pio/
src/secrets.h
```

- [ ] **Step 4: Write `src/main.cpp` (serial + LED heartbeat)**

```cpp
#include <Arduino.h>

constexpr uint8_t LED_PIN = 21; // XIAO user LED, active-LOW

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    // USB-CDC needs a moment before prints are visible
    delay(2000);
    Serial.println("ee02-playground: task 1 — serial hello");
}

void loop() {
    digitalWrite(LED_PIN, LOW);  // LED on
    delay(500);
    digitalWrite(LED_PIN, HIGH); // LED off
    delay(500);
    Serial.println("heartbeat");
}
```

- [ ] **Step 5: Build**

```bash
cd /Users/sven/Developer/ee02-playground && pio run
```

Expected: first run downloads the ESP32 platform + toolchain (several minutes), then `SUCCESS`. RAM/Flash usage lines printed.

- [ ] **Step 6: Flash and observe**

Plug the EE02 in via USB-C, power switch ON.

```bash
pio run -t upload
pio device monitor
```

Expected: upload ends with `Hash of data verified` / `SUCCESS`; monitor shows `heartbeat` once per second; XIAO's orange user LED blinks. Ctrl-C exits the monitor. If no port: hold BOOT while plugging in, retry upload, press RESET.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini .gitignore src/main.cpp
git commit -m "Task 1: PlatformIO scaffold, serial+LED heartbeat"
```

---

### Task 2: Milestone 1 — hello display (six-color test pattern)

**Files:**
- Modify: `src/main.cpp` (replace entirely)

**Interfaces:**
- Consumes: project scaffold from Task 1.
- Produces: global `EPaper epaper;` object and the six-color array `const uint16_t COLORS[6]` with names `COLOR_NAMES[6]` — Task 3 reuses both; the draw-then-`epaper.update()` idiom all later tasks follow.

- [ ] **Step 1: Replace `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper when the ePaper combo is selected

EPaper epaper;

const uint16_t COLORS[6] = {TFT_BLACK, TFT_WHITE, TFT_RED,
                            TFT_YELLOW, TFT_GREEN, TFT_BLUE};
const char *COLOR_NAMES[6] = {"BLACK", "WHITE", "RED",
                              "YELLOW", "GREEN", "BLUE"};

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("ee02-playground: task 2 — six-color test pattern");

    epaper.begin();
    Serial.printf("panel: %d x %d\n", epaper.width(), epaper.height());

    epaper.fillScreen(TFT_WHITE);

    // Six vertical bars, 200 px each across the 1200 px width
    for (int i = 0; i < 6; i++) {
        epaper.fillRect(i * 200, 0, 200, 1200, COLORS[i]);
    }

    // Labels under the bars, on white background
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    for (int i = 0; i < 6; i++) {
        epaper.drawString(COLOR_NAMES[i], i * 200 + 20, 1250, 4);
    }
    epaper.drawString("ee02-playground / milestone 1", 20, 1400, 4);

    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done");
}

void loop() {}
```

- [ ] **Step 2: Build**

```bash
pio run
```

Expected: `SUCCESS`. If the compiler cannot find `EPaper`, the two `build_flags` defines are missing/typoed — fix `platformio.ini`, not the code.

- [ ] **Step 3: Flash and observe**

```bash
pio run -t upload && pio device monitor
```

Expected: serial prints `panel: 1200 x 1600`, then `updating panel...`; after ~20–30 s of flickering the panel shows six labeled color bars. This step also proves the Plus flash/PSRAM overrides are correct — if the panel renders, both unknowns from the spec are resolved.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Task 2: six-color test pattern on 13.3\" Spectra 6"
```

---

### Task 3: Milestone 2 — buttons switch demo screens

**Files:**
- Modify: `src/main.cpp` (replace entirely)

**Interfaces:**
- Consumes: `EPaper epaper`, `COLORS`/`COLOR_NAMES` from Task 2.
- Produces: button constants `BTN_PREV=2, BTN_REFRESH=3, BTN_NEXT=5` and the poll/debounce pattern; `void showScreen(int idx)` dispatcher. Task 4 keeps the buttons; Task 5 reuses the pin constants as wake sources.

- [ ] **Step 1: Replace `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <TFT_eSPI.h>

EPaper epaper;

const uint16_t COLORS[6] = {TFT_BLACK, TFT_WHITE, TFT_RED,
                            TFT_YELLOW, TFT_GREEN, TFT_BLUE};
const char *COLOR_NAMES[6] = {"BLACK", "WHITE", "RED",
                              "YELLOW", "GREEN", "BLUE"};

// EE02 user buttons: active-LOW, external pull-ups on board
constexpr uint8_t BTN_PREV = 2;
constexpr uint8_t BTN_REFRESH = 3;
constexpr uint8_t BTN_NEXT = 5;
constexpr uint8_t LED_PIN = 21; // active-LOW

constexpr int SCREEN_COUNT = 3;
int screenIndex = 0;

void drawColorBars() {
    epaper.fillScreen(TFT_WHITE);
    for (int i = 0; i < 6; i++)
        epaper.fillRect(i * 200, 0, 200, 1200, COLORS[i]);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    for (int i = 0; i < 6; i++)
        epaper.drawString(COLOR_NAMES[i], i * 200 + 20, 1250, 4);
    epaper.drawString("screen 1/3: color bars", 20, 1400, 4);
}

void drawInfoScreen() {
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString("screen 2/3: info", 20, 40, 4);
    epaper.setTextColor(TFT_RED, TFT_WHITE);
    epaper.drawString("XIAO ePaper Display Board EE02", 20, 140, 4);
    epaper.setTextColor(TFT_BLUE, TFT_WHITE);
    epaper.drawString("13.3\" Spectra 6, 1200 x 1600", 20, 220, 4);
    epaper.setTextColor(TFT_GREEN, TFT_WHITE);
    epaper.drawString("buttons: GPIO2 prev / GPIO3 refresh / GPIO5 next",
                      20, 300, 4);
    epaper.setTextColor(TFT_BLACK, TFT_YELLOW);
    epaper.drawString("uptime (ms): " + String(millis()), 20, 380, 4);
}

void drawCheckerboard() {
    epaper.fillScreen(TFT_WHITE);
    for (int y = 0; y < 1600; y += 100)
        for (int x = 0; x < 1200; x += 100)
            epaper.fillRect(x, y, 100, 100,
                            COLORS[((x + y) / 100) % 6]);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString("screen 3/3: checkerboard", 20, 20, 4);
}

void showScreen(int idx) {
    digitalWrite(LED_PIN, LOW); // LED on = busy
    Serial.printf("drawing screen %d (update takes ~20-30 s)...\n", idx + 1);
    switch (idx) {
        case 0: drawColorBars(); break;
        case 1: drawInfoScreen(); break;
        case 2: drawCheckerboard(); break;
    }
    epaper.update();
    digitalWrite(LED_PIN, HIGH); // LED off = ready
    Serial.println("ready — press a button");
}

// Returns true once per physical press (falling edge + debounce)
bool pressed(uint8_t pin) {
    if (digitalRead(pin) == LOW) {
        delay(30); // debounce
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) delay(10); // wait for release
            return true;
        }
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("ee02-playground: task 3 — button demo");

    pinMode(BTN_PREV, INPUT);    // external pull-ups on board
    pinMode(BTN_REFRESH, INPUT);
    pinMode(BTN_NEXT, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    epaper.begin();
    showScreen(screenIndex);
}

void loop() {
    if (pressed(BTN_NEXT)) {
        screenIndex = (screenIndex + 1) % SCREEN_COUNT;
        showScreen(screenIndex);
    } else if (pressed(BTN_PREV)) {
        screenIndex = (screenIndex + SCREEN_COUNT - 1) % SCREEN_COUNT;
        showScreen(screenIndex);
    } else if (pressed(BTN_REFRESH)) {
        showScreen(screenIndex);
    }
    delay(10);
}
```

- [ ] **Step 2: Build**

```bash
pio run
```

Expected: `SUCCESS`.

- [ ] **Step 3: Flash and observe**

```bash
pio run -t upload && pio device monitor
```

Expected: color bars appear; pressing **next** shows the info screen, **next** again the checkerboard, **prev** goes back, **refresh** redraws the current screen. LED is lit during each ~20–30 s refresh; presses during a refresh are ignored (the update call blocks — that's expected e-ink behavior, note it and move on).

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Task 3: three demo screens switched by EE02 user buttons"
```

---

### Task 4: Milestone 3 — Wi-Fi captive-portal provisioning + picsum image fetch

**Files:**
- Modify: `platformio.ini` (add WiFiManager + JPEGDecoder)
- Modify: `src/main.cpp` (replace entirely)

**Interfaces:**
- Consumes: `EPaper epaper`, button constants and `pressed()` from Task 3.
- Produces: `bool connectWifi()` (WiFiManager autoConnect: saved-credentials connect or captive portal), `bool fetchAndShowImage()`, `void showError(const String&)` — Task 5 reuses all three. The forget-wifi-at-boot gesture (refresh held at power-on).

- [ ] **Step 1: Add libraries to `platformio.ini`**

Replace the `lib_deps` block with:

```ini
lib_deps =
    https://github.com/Seeed-Studio/Seeed_GFX.git
    bodmer/JPEGDecoder@^2.0.0
    tzapu/WiFiManager@^2.0.17
```

- [ ] **Step 2: Replace `src/main.cpp`**

```cpp
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
```

- [ ] **Step 3: Build**

```bash
pio run
```

Expected: `SUCCESS` (WiFiManager and JPEGDecoder download on first build).

- [ ] **Step 4: Flash and observe first-boot provisioning**

```bash
pio run -t upload && pio device monitor
```

Expected serial (no credentials saved yet): `connecting (saved credentials, or captive portal)...` then `config portal up: join "EE02-Setup", then open http://192.168.4.1` and `instructions on panel`; the panel shows the setup instructions.

**User steps (cannot be automated):** join `EE02-Setup` from a phone, open `http://192.168.4.1`, pick the home network from the scan list (signal strengths shown), enter the password. Then expected serial: `connected to <ssid>, IP …`, `GET https://images.weserv.nl/…`, `content-length: …`, `jpeg: 1200 x 1600 …`, `updating panel…`; a random photo appears (six-color dithered — posterized colors are the medium, not a bug). Pressing **refresh** fetches a different image. Power-cycling the board must reconnect without the portal.

Note: picsum direct URLs serve progressive JPEGs (JPEGDecoder decodes them as 0×0 — observed on hardware 2026-07-08), which is why the URL goes through images.weserv.nl for baseline re-encode. If serial ever shows `jpeg: 0 x 0` again, the proxy output changed — re-verify with `curl -sL '<url>' -o x.jpg && file x.jpg` (must say `baseline`).

- [ ] **Step 5: Commit**

```bash
git add platformio.ini src/main.cpp
git commit -m "Task 4: WiFiManager captive-portal provisioning + picsum JPEG fetch"
```

---

### Task 5: Milestone 4 — battery reading + deep sleep

**Files:**
- Modify: `src/main.cpp` (replace entirely)

**Interfaces:**
- Consumes: `connectWifi()` (WiFiManager), `fetchAndShowImage()` pattern, `showError()`, button/pin constants from Task 4.
- Produces: final firmware shape: boot → (optional forget-wifi) → connect → fetch → overlay status footer → deep sleep (timer + any-button wake). `float readBatteryVoltage()` and RTC-persistent `bootCount`.

- [ ] **Step 1: Replace `src/main.cpp`**

```cpp
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

float readBatteryVoltage() {
    analogReadResolution(12);
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BATTERY_EN_PIN, OUTPUT);
    digitalWrite(BATTERY_EN_PIN, HIGH);
    delay(5);
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += analogRead(BATTERY_ADC_PIN);
        delay(2);
    }
    digitalWrite(BATTERY_EN_PIN, LOW);
    return (sum / 10 / 4095.0f) * 3.3f * 2.0f; // /2 divider
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

void drawStatusFooter(float vbat) {
    String status = "boot #" + String(bootCount) + "  wake: " +
                    wakeReason() + "  vbat: " + String(vbat, 2) + " V";
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

    float vbat = readBatteryVoltage();
    Serial.printf("battery: %.2f V\n", vbat);

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
        drawStatusFooter(vbat);
        Serial.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        Serial.println("done");
    }

    digitalWrite(LED_PIN, HIGH);
    goToSleep(); // never returns
}

void loop() {}
```

- [ ] **Step 2: Build**

```bash
pio run
```

Expected: `SUCCESS`.

- [ ] **Step 3: Flash and observe the full cycle**

```bash
pio run -t upload && pio device monitor
```

Expected: serial shows `boot #1, wake: power-on/reset`, a battery voltage line (≈3.5–4.2 V with a battery; USB-only readings may sit higher — record what appears), `connected to <ssid>…` (saved credentials from Task 4 — no portal), image renders with footer `boot #1  wake: power-on/reset  vbat: …`, then `sleeping 3600 s…` and serial goes quiet (deep sleep drops the USB port — expected).

- [ ] **Step 4: Verify button wake (user step)**

Press any of the three user buttons. The USB port re-enumerates; re-run `pio device monitor` quickly.

Expected: `boot #2, wake: button` and a fresh image with updated footer. `bootCount` persisting across sleep proves RTC memory works.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "Task 5: battery reading, status footer, deep sleep with timer+button wake"
```

---

## Done criteria (maps to spec)

- Milestone 1 (hello display) → Task 2
- Milestone 2 (buttons & LED) → Task 3
- Milestone 3 (Wi-Fi captive-portal provisioning, picsum fetch, error screens) → Task 4
- Milestone 4 (deep sleep & battery) → Task 5
- Toolchain/scaffold, PSRAM/flash overrides, checked-in display config (as build flags) → Task 1
