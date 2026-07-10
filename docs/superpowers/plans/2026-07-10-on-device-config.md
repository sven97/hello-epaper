# On-Device Config Portal + Open-Source Prep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every runtime behavior (refresh interval, image URL, quiet hours, timezone, name, orientation, pin state) becomes configurable from a web page the device itself serves while its status page is displayed; plus LICENSE, README, CI, dependency pinning, and native unit tests to make the repo open-source ready.

**Architecture:** Pure decision logic lives in header-only modules under `src/logic/` (no Arduino deps) so it unit-tests on the host via PlatformIO's `native` env. Firmware modules (`settings`, `portal`) wrap that logic. KEY1 shows a status page and starts a web portal (ESP32 `WebServer` + mDNS); exit always runs a fetch cycle and sleeps. Spec: `docs/superpowers/specs/2026-07-10-on-device-config-design.md`.

**Tech Stack:** PlatformIO (espressif32 / Arduino), Seeed_GFX (pinned), WiFiManager, JPEGDecoder, ricmoo/QRCode, Unity (native tests), GitHub Actions.

## Global Constraints

- NVS namespace stays `frame`; existing keys `held`, `lastEpoch`, `wifiDesc`, `tzOff` keep their meaning. The `info` key is removed (status page no longer persists).
- Headers under `src/logic/` MUST compile without Arduino.h — `<cstdint>`, `<string>` only. All Arduino types stay in `src/*.{h,cpp}`.
- Defaults must exactly reproduce today's behavior: 1 h interval, weserv/picsum URL, no quiet hours, auto timezone, name `ee02`, portrait.
- Never edit files under `.pio/`. Display driver selection stays the two `build_flags` in `platformio.ini`.
- Build check is `pio run -e ee02`; native tests are `pio test -e native`. Both must pass before every commit.
- Commit style follows the repo: imperative summary, no conventional-commit prefixes (see `git log`).
- Panel truths: 4 bpp palette-indexed framebuffer (`FRAME_BYTES = 1600*1200/2` regardless of rotation), full refresh takes ~25–30 s, `TFT_*` macros are palette nibbles.

---

### Task 1: Test/CI infrastructure, LICENSE, dependency pinning

**Files:**
- Create: `LICENSE`
- Create: `src/logic/battery_curve.h`
- Create: `test/test_battery/main.cpp`
- Create: `.github/workflows/ci.yml`
- Modify: `platformio.ini`
- Modify: `src/power.cpp` (lines 44–66: `batteryPercent`)

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `int batteryPercentFromMv(int mv)` in `src/logic/battery_curve.h`; a working `pio test -e native` harness and CI that later tasks add tests to. `src/power.cpp`'s public `int batteryPercent(int32_t mv)` is unchanged for callers.

- [ ] **Step 1: Update `platformio.ini`** — pin Seeed_GFX to the commit both the lockdir and upstream HEAD sit at today, add the QRCode dependency (used by Task 9), and add the native test env:

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
    https://github.com/Seeed-Studio/Seeed_GFX.git#a2de1abca0597c202193f22d01e9fa35d1ff613b
    bodmer/JPEGDecoder@^2.0.0
    tzapu/WiFiManager@^2.0.17
    ricmoo/QRCode@^0.0.1

build_flags =
    -DBOARD_SCREEN_COMBO=510
    -DUSE_XIAO_EPAPER_DISPLAY_BOARD_EE02

; Host-side unit tests for the pure logic in src/logic/ (no Arduino deps).
[env:native]
platform = native
test_framework = unity
test_build_src = false
build_src_filter = -<*>
build_flags = -std=gnu++17 -I src
```

- [ ] **Step 2: Write the failing test** — `test/test_battery/main.cpp`:

```cpp
#include <unity.h>
#include "logic/battery_curve.h"

void setUp() {}
void tearDown() {}

void test_full_at_4200() { TEST_ASSERT_EQUAL(100, batteryPercentFromMv(4200)); }
void test_clamped_above() { TEST_ASSERT_EQUAL(100, batteryPercentFromMv(4350)); }
void test_empty_at_3000() { TEST_ASSERT_EQUAL(0, batteryPercentFromMv(3000)); }
void test_clamped_below() { TEST_ASSERT_EQUAL(0, batteryPercentFromMv(2500)); }
void test_interpolates_midcurve() { TEST_ASSERT_EQUAL(38, batteryPercentFromMv(3650)); }
void test_interpolates_top() { TEST_ASSERT_EQUAL(97, batteryPercentFromMv(4150)); }
void test_interpolates_bottom() { TEST_ASSERT_EQUAL(2, batteryPercentFromMv(3250)); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_full_at_4200);
    RUN_TEST(test_clamped_above);
    RUN_TEST(test_empty_at_3000);
    RUN_TEST(test_clamped_below);
    RUN_TEST(test_interpolates_midcurve);
    RUN_TEST(test_interpolates_top);
    RUN_TEST(test_interpolates_bottom);
    return UNITY_END();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `pio test -e native`
Expected: FAIL — `logic/battery_curve.h: No such file or directory`

- [ ] **Step 4: Create `src/logic/battery_curve.h`** — the curve moves verbatim from `power.cpp`:

```cpp
#pragma once
// Rough Li-ion state-of-charge from resting voltage. Pure logic: host-testable.

inline int batteryPercentFromMv(int mv) {
    struct Point { short mv; unsigned char pct; };
    static const Point CURVE[] = {
        {4200, 100}, {4100, 94}, {4000, 85}, {3900, 74}, {3800, 62},
        {3700, 48},  {3600, 29}, {3500, 13}, {3400, 6},  {3300, 3},
        {3200, 1},   {3000, 0},
    };
    const int N = sizeof(CURVE) / sizeof(CURVE[0]);
    if (mv >= CURVE[0].mv) return 100;
    if (mv <= CURVE[N - 1].mv) return 0;
    for (int i = 1; i < N; i++) {
        if (mv >= CURVE[i].mv) {
            return CURVE[i].pct +
                   (int)(mv - CURVE[i].mv) *
                       (CURVE[i - 1].pct - CURVE[i].pct) /
                       (CURVE[i - 1].mv - CURVE[i].mv);
        }
    }
    return 0;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `7 Tests 0 Failures` — PASS

- [ ] **Step 6: Point `power.cpp` at the shared curve.** In `src/power.cpp`, add `#include "logic/battery_curve.h"` after the existing includes, then replace the whole `batteryPercent` function body (the `struct Point`…`return 0;` block, currently lines 47–66) with:

```cpp
// Rough state-of-charge from a typical Li-ion discharge curve (resting
// voltage). No fuel gauge on board, so this is an estimate: reads a few
// percent high while charging and low under load.
int batteryPercent(int32_t mv) { return batteryPercentFromMv((int)mv); }
```

- [ ] **Step 7: Verify firmware still builds**

Run: `pio run -e ee02`
Expected: SUCCESS (Seeed_GFX re-resolves against the pinned SHA — same commit as before, so no code change)

- [ ] **Step 8: Add `LICENSE`** (MIT, root):

```
MIT License

Copyright (c) 2026 Sven Chiu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 9: Add `.github/workflows/ci.yml`:**

```yaml
name: ci
on:
  push:
    branches: [main]
  pull_request:

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - name: Cache PlatformIO
        uses: actions/cache@v4
        with:
          path: ~/.platformio
          key: pio-${{ hashFiles('platformio.ini') }}
      - run: pip install platformio
      - name: Native unit tests
        run: pio test -e native
      - name: Firmware build
        run: pio run -e ee02
```

- [ ] **Step 10: Commit**

```bash
git add LICENSE platformio.ini src/logic/battery_curve.h src/power.cpp test/test_battery/ .github/workflows/ci.yml
git commit -m "Test/CI infra: native test env + first pure-logic module, MIT license, pin Seeed_GFX"
```

---

### Task 2: URL template logic

**Files:**
- Create: `src/logic/url_template.h`
- Test: `test/test_url_template/main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `std::string renderUrlTemplate(const std::string &tmpl, unsigned long seed, int width, int height)` — replaces every `{seed}`, `{width}`, `{height}` occurrence. Task 6 calls it from `net.cpp`.

- [ ] **Step 1: Write the failing test** — `test/test_url_template/main.cpp`:

```cpp
#include <unity.h>
#include "logic/url_template.h"

void setUp() {}
void tearDown() {}

void test_replaces_all_tokens() {
    std::string out = renderUrlTemplate(
        "https://x/{width}/{height}?r={seed}", 42UL, 1200, 1600);
    TEST_ASSERT_EQUAL_STRING("https://x/1200/1600?r=42", out.c_str());
}

void test_repeated_token() {
    std::string out = renderUrlTemplate("{seed}-{seed}", 7UL, 1, 1);
    TEST_ASSERT_EQUAL_STRING("7-7", out.c_str());
}

void test_no_tokens_passthrough() {
    std::string out = renderUrlTemplate("https://example.com/pic.jpg", 9UL, 1200, 1600);
    TEST_ASSERT_EQUAL_STRING("https://example.com/pic.jpg", out.c_str());
}

void test_landscape_dims() {
    std::string out = renderUrlTemplate("{width}x{height}", 0UL, 1600, 1200);
    TEST_ASSERT_EQUAL_STRING("1600x1200", out.c_str());
}

void test_empty_template() {
    TEST_ASSERT_EQUAL_STRING("", renderUrlTemplate("", 1UL, 2, 3).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_replaces_all_tokens);
    RUN_TEST(test_repeated_token);
    RUN_TEST(test_no_tokens_passthrough);
    RUN_TEST(test_landscape_dims);
    RUN_TEST(test_empty_template);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_url_template`
Expected: FAIL — `logic/url_template.h: No such file or directory`

- [ ] **Step 3: Create `src/logic/url_template.h`:**

```cpp
#pragma once
// Image-URL templating. Pure logic: host-testable, no Arduino deps.
#include <string>

inline void replaceAll(std::string &s, const std::string &from,
                       const std::string &to) {
    for (size_t pos = s.find(from); pos != std::string::npos;
         pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

// Substitute {seed}, {width}, {height}. Templates without tokens pass
// through untouched (static image URLs are valid sources).
inline std::string renderUrlTemplate(const std::string &tmpl,
                                     unsigned long seed, int width,
                                     int height) {
    std::string out = tmpl;
    replaceAll(out, "{seed}", std::to_string(seed));
    replaceAll(out, "{width}", std::to_string(width));
    replaceAll(out, "{height}", std::to_string(height));
    return out;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_url_template`
Expected: `5 Tests 0 Failures` — PASS

- [ ] **Step 5: Commit**

```bash
git add src/logic/url_template.h test/test_url_template/
git commit -m "URL template logic: {seed}/{width}/{height} substitution"
```

---

### Task 3: Quiet-hours logic

**Files:**
- Create: `src/logic/quiet_hours.h`
- Test: `test/test_quiet_hours/main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (all take seconds-of-local-day `nowSod` in `[0, 86400)` and whole hours `startHour`/`endHour` in `[0, 23]`; window is `[start, end)` and `start == end` means "no window"):
  - `bool inQuietWindow(int nowSod, int startHour, int endHour)`
  - `uint32_t quietAdjustedSleep(int nowSod, uint32_t sleepSecs, bool enabled, int startHour, int endHour)` — extends sleep so the wake lands at window end when it would land inside.
  - `uint32_t secondsUntilQuietEnd(int nowSod, int startHour, int endHour)` — 0 when outside the window.
  - Task 8 calls these from `power.cpp` and `main.cpp`; Task 10 uses `quietAdjustedSleep` for the "next:" line.

- [ ] **Step 1: Write the failing test** — `test/test_quiet_hours/main.cpp`:

```cpp
#include <unity.h>
#include "logic/quiet_hours.h"

void setUp() {}
void tearDown() {}

static const int H = 3600;

void test_window_simple() {
    TEST_ASSERT_TRUE(inQuietWindow(2 * H, 1, 5));
    TEST_ASSERT_FALSE(inQuietWindow(6 * H, 1, 5));
    TEST_ASSERT_TRUE(inQuietWindow(1 * H, 1, 5));      // start inclusive
    TEST_ASSERT_FALSE(inQuietWindow(5 * H, 1, 5));     // end exclusive
}

void test_window_wraps_midnight() {
    TEST_ASSERT_TRUE(inQuietWindow(23 * H + 1800, 23, 7));
    TEST_ASSERT_TRUE(inQuietWindow(3 * H, 23, 7));
    TEST_ASSERT_FALSE(inQuietWindow(12 * H, 23, 7));
}

void test_equal_hours_is_no_window() {
    TEST_ASSERT_FALSE(inQuietWindow(5 * H, 5, 5));
}

void test_sleep_untouched_outside() {
    // 12:00 + 1h = 13:00, outside 23-7.
    TEST_ASSERT_EQUAL_UINT32(3600, quietAdjustedSleep(12 * H, 3600, true, 23, 7));
}

void test_sleep_untouched_when_disabled() {
    TEST_ASSERT_EQUAL_UINT32(3600, quietAdjustedSleep(23 * H, 3600, false, 23, 7));
}

void test_sleep_extends_to_window_end_wrapped() {
    // 22:30 + 1h = 23:30 inside 23-7 -> sleep until 07:00 = 8.5h.
    TEST_ASSERT_EQUAL_UINT32(30600, quietAdjustedSleep(22 * H + 1800, 3600, true, 23, 7));
}

void test_sleep_extends_simple_window() {
    // 00:30 + 1h = 01:30 inside 1-5 -> until 05:00 = 4.5h.
    TEST_ASSERT_EQUAL_UINT32(16200, quietAdjustedSleep(1800, 3600, true, 1, 5));
}

void test_wake_exactly_at_end_passes() {
    // 04:00 + 1h = 05:00 = window end (exclusive) -> untouched.
    TEST_ASSERT_EQUAL_UINT32(3600, quietAdjustedSleep(4 * H, 3600, true, 1, 5));
}

void test_seconds_until_end() {
    TEST_ASSERT_EQUAL_UINT32(0, secondsUntilQuietEnd(12 * H, 23, 7));
    TEST_ASSERT_EQUAL_UINT32(4 * H, secondsUntilQuietEnd(3 * H, 23, 7));
    TEST_ASSERT_EQUAL_UINT32(8 * H, secondsUntilQuietEnd(23 * H, 23, 7));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_window_simple);
    RUN_TEST(test_window_wraps_midnight);
    RUN_TEST(test_equal_hours_is_no_window);
    RUN_TEST(test_sleep_untouched_outside);
    RUN_TEST(test_sleep_untouched_when_disabled);
    RUN_TEST(test_sleep_extends_to_window_end_wrapped);
    RUN_TEST(test_sleep_extends_simple_window);
    RUN_TEST(test_wake_exactly_at_end_passes);
    RUN_TEST(test_seconds_until_end);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_quiet_hours`
Expected: FAIL — `logic/quiet_hours.h: No such file or directory`

- [ ] **Step 3: Create `src/logic/quiet_hours.h`:**

```cpp
#pragma once
// Quiet-hours window math. Pure logic: host-testable, no Arduino deps.
// Times are seconds-of-local-day [0, 86400); the window is [start, end)
// in whole hours and may wrap midnight. start == end means "no window".
#include <cstdint>

constexpr int SECONDS_PER_DAY = 86400;

inline bool inQuietWindow(int nowSod, int startHour, int endHour) {
    if (startHour == endHour) return false;
    const int s = startHour * 3600, e = endHour * 3600;
    if (s < e) return nowSod >= s && nowSod < e;
    return nowSod >= s || nowSod < e; // wraps midnight
}

inline uint32_t secondsUntilQuietEnd(int nowSod, int startHour, int endHour) {
    if (!inQuietWindow(nowSod, startHour, endHour)) return 0;
    const int e = endHour * 3600;
    return (uint32_t)((e - nowSod + SECONDS_PER_DAY) % SECONDS_PER_DAY);
}

// If the nominal wake (now + sleepSecs) lands inside the window, sleep
// through to the window end instead. sleepSecs is at most 24 h.
inline uint32_t quietAdjustedSleep(int nowSod, uint32_t sleepSecs,
                                   bool enabled, int startHour, int endHour) {
    if (!enabled) return sleepSecs;
    const int wakeSod = (int)((nowSod + sleepSecs) % SECONDS_PER_DAY);
    const uint32_t extra = secondsUntilQuietEnd(wakeSod, startHour, endHour);
    return sleepSecs + extra;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_quiet_hours`
Expected: `9 Tests 0 Failures` — PASS

- [ ] **Step 5: Commit**

```bash
git add src/logic/quiet_hours.h test/test_quiet_hours/
git commit -m "Quiet-hours logic: window test, sleep extension, midnight wrap"
```

---

### Task 4: Settings validation logic

**Files:**
- Create: `src/logic/validate.h`
- Test: `test/test_validate/main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (Task 9's `POST /save` handler is the caller):
  - `bool isValidSleepSecs(uint32_t s)` — one of 900, 1800, 3600, 7200, 14400, 28800, 43200, 86400.
  - `bool isValidImageUrl(const std::string &u)` — `http://` or `https://` prefix, total length 12–512.
  - `bool isValidHour(int h)` — 0–23.
  - `bool isValidDeviceName(const std::string &n)` — `[a-z0-9-]`, length 1–24, no leading/trailing `-`.
  - `bool isValidTzOffsetSec(long o)` — within ±14 h, multiple of 900 (15 min).
  - `bool isValidRotation(int r)` — 0–3.

- [ ] **Step 1: Write the failing test** — `test/test_validate/main.cpp`:

```cpp
#include <unity.h>
#include "logic/validate.h"

void setUp() {}
void tearDown() {}

void test_sleep_choices() {
    TEST_ASSERT_TRUE(isValidSleepSecs(900));
    TEST_ASSERT_TRUE(isValidSleepSecs(3600));
    TEST_ASSERT_TRUE(isValidSleepSecs(86400));
    TEST_ASSERT_FALSE(isValidSleepSecs(0));
    TEST_ASSERT_FALSE(isValidSleepSecs(3601));
}

void test_image_url() {
    TEST_ASSERT_TRUE(isValidImageUrl("https://example.com/a.jpg"));
    TEST_ASSERT_TRUE(isValidImageUrl("http://192.168.1.5/pic"));
    TEST_ASSERT_FALSE(isValidImageUrl("ftp://example.com/a.jpg"));
    TEST_ASSERT_FALSE(isValidImageUrl("https://"));
    TEST_ASSERT_FALSE(isValidImageUrl(std::string("https://") + std::string(510, 'a')));
}

void test_hours() {
    TEST_ASSERT_TRUE(isValidHour(0));
    TEST_ASSERT_TRUE(isValidHour(23));
    TEST_ASSERT_FALSE(isValidHour(24));
    TEST_ASSERT_FALSE(isValidHour(-1));
}

void test_device_name() {
    TEST_ASSERT_TRUE(isValidDeviceName("ee02"));
    TEST_ASSERT_TRUE(isValidDeviceName("living-room-frame"));
    TEST_ASSERT_FALSE(isValidDeviceName(""));
    TEST_ASSERT_FALSE(isValidDeviceName("Big Frame"));
    TEST_ASSERT_FALSE(isValidDeviceName("-frame"));
    TEST_ASSERT_FALSE(isValidDeviceName("frame-"));
    TEST_ASSERT_FALSE(isValidDeviceName("abcdefghijklmnopqrstuvwxy")); // 25
}

void test_tz_offset() {
    TEST_ASSERT_TRUE(isValidTzOffsetSec(0));
    TEST_ASSERT_TRUE(isValidTzOffsetSec(28800));    // UTC+8
    TEST_ASSERT_TRUE(isValidTzOffsetSec(-12600));   // UTC-3:30
    TEST_ASSERT_FALSE(isValidTzOffsetSec(50401));
    TEST_ASSERT_FALSE(isValidTzOffsetSec(100));     // not a 15-min step
}

void test_rotation() {
    TEST_ASSERT_TRUE(isValidRotation(0));
    TEST_ASSERT_TRUE(isValidRotation(3));
    TEST_ASSERT_FALSE(isValidRotation(4));
    TEST_ASSERT_FALSE(isValidRotation(-1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sleep_choices);
    RUN_TEST(test_image_url);
    RUN_TEST(test_hours);
    RUN_TEST(test_device_name);
    RUN_TEST(test_tz_offset);
    RUN_TEST(test_rotation);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_validate`
Expected: FAIL — `logic/validate.h: No such file or directory`

- [ ] **Step 3: Create `src/logic/validate.h`:**

```cpp
#pragma once
// Server-side validation for portal form input. Pure logic: host-testable.
#include <cstdint>
#include <string>

inline bool isValidSleepSecs(uint32_t s) {
    static const uint32_t CHOICES[] = {900,  1800,  3600,  7200,
                                       14400, 28800, 43200, 86400};
    for (uint32_t c : CHOICES)
        if (s == c) return true;
    return false;
}

inline bool isValidImageUrl(const std::string &u) {
    if (u.size() < 12 || u.size() > 512) return false;
    return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0;
}

inline bool isValidHour(int h) { return h >= 0 && h <= 23; }

inline bool isValidDeviceName(const std::string &n) {
    if (n.empty() || n.size() > 24) return false;
    if (n.front() == '-' || n.back() == '-') return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}

inline bool isValidTzOffsetSec(long o) {
    return o >= -14L * 3600 && o <= 14L * 3600 && o % 900 == 0;
}

inline bool isValidRotation(int r) { return r >= 0 && r <= 3; }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_validate`
Expected: `6 Tests 0 Failures` — PASS

- [ ] **Step 5: Commit**

```bash
git add src/logic/validate.h test/test_validate/
git commit -m "Portal input validation logic"
```

---

### Task 5: Settings module

**Files:**
- Create: `src/settings.h`, `src/settings.cpp`
- Modify: `src/config.h` (rename `SLEEP_SECONDS`, add defaults)
- Modify: `src/state.h` (document new keys)

**Interfaces:**
- Consumes: `prefs` (extern `Preferences`, `state.h`), defaults from `config.h`.
- Produces (everything later tasks read):

```cpp
struct Settings {
    uint32_t sleepSecs;      // seconds between refreshes
    String   imageUrl;       // template: {seed} {width} {height}
    bool     quietEnabled;
    uint8_t  quietStartHour; // 0-23, window [start, end) local time
    uint8_t  quietEndHour;
    bool     tzAuto;         // true: ip-api detect; false: manual tzOff
    String   name;           // mDNS hostname, [a-z0-9-]{1,24}
    uint8_t  rotation;       // epaper.setRotation() arg, 0-3
};
extern Settings settings;
void loadSettings();  // prefs.begin() must have run; missing keys -> defaults
void saveSettings();  // writes every field to NVS
```

- [ ] **Step 1: Update `src/config.h`.** Replace the `// ---- Behavior ----` section (currently `SLEEP_SECONDS`, `AP_NAME`, `TZ_API_URL`) with:

```cpp
// ---- Behavior defaults (runtime values live in settings.h / NVS) --------
constexpr uint32_t DEFAULT_SLEEP_SECONDS = 60 * 60; // 1 hour
inline const char *DEFAULT_IMAGE_URL =
    "https://images.weserv.nl/?url=picsum.photos/{width}/{height}"
    "%3Frandom%3D{seed}&output=jpg";
inline const char *DEFAULT_DEVICE_NAME = "ee02";
constexpr uint8_t DEFAULT_ROTATION = 0; // portrait

inline const char *AP_NAME = "EE02-Setup";
inline const char *TZ_API_URL =
    "http://ip-api.com/json?fields=status,timezone,offset";
```

(Keep `FRAME_PATH`, `FRAME_BYTES`, `CLOCK_SANE_EPOCH` as they are. The build now fails everywhere `SLEEP_SECONDS` was used — that is intentional and fixed in Steps 3–4.)

- [ ] **Step 2: Create `src/settings.h`:**

```cpp
#pragma once
#include <Arduino.h>

// Runtime configuration, NVS-backed (namespace "frame"). Defaults in
// config.h reproduce the original fixed behavior exactly.
struct Settings {
    uint32_t sleepSecs;      // seconds between refreshes
    String   imageUrl;       // template: {seed} {width} {height}
    bool     quietEnabled;
    uint8_t  quietStartHour; // 0-23, window [start, end) local time
    uint8_t  quietEndHour;
    bool     tzAuto;         // true: ip-api detect; false: manual tzOff
    String   name;           // mDNS hostname, [a-z0-9-]{1,24}
    uint8_t  rotation;       // epaper.setRotation() arg, 0-3
};

extern Settings settings;

void loadSettings();  // call after prefs.begin(); missing keys -> defaults
void saveSettings();  // persist every field
```

and `src/settings.cpp`:

```cpp
#include "settings.h"
#include "config.h"
#include "state.h"

Settings settings;

// NVS keys (<=15 chars): sleepSecs imgUrl quietEn quietSh quietEh tzAuto
// devName rot. The operative UTC offset stays in the pre-existing "tzOff"
// key (written by auto-detect or by the portal in manual mode).
void loadSettings() {
    settings.sleepSecs = prefs.getUInt("sleepSecs", DEFAULT_SLEEP_SECONDS);
    settings.imageUrl = prefs.getString("imgUrl", DEFAULT_IMAGE_URL);
    settings.quietEnabled = prefs.getBool("quietEn", false);
    settings.quietStartHour = prefs.getUChar("quietSh", 23);
    settings.quietEndHour = prefs.getUChar("quietEh", 7);
    settings.tzAuto = prefs.getBool("tzAuto", true);
    settings.name = prefs.getString("devName", DEFAULT_DEVICE_NAME);
    settings.rotation = prefs.getUChar("rot", DEFAULT_ROTATION);
}

void saveSettings() {
    prefs.putUInt("sleepSecs", settings.sleepSecs);
    prefs.putString("imgUrl", settings.imageUrl);
    prefs.putBool("quietEn", settings.quietEnabled);
    prefs.putUChar("quietSh", settings.quietStartHour);
    prefs.putUChar("quietEh", settings.quietEndHour);
    prefs.putBool("tzAuto", settings.tzAuto);
    prefs.putString("devName", settings.name);
    prefs.putUChar("rot", settings.rotation);
}
```

- [ ] **Step 3: Fix the `SLEEP_SECONDS` fallout.** Replace every remaining use with `settings.sleepSecs` (add `#include "settings.h"` where missing):
  - `src/main.cpp:197` (dev-loop `fetchDue`): `fetchDue = time(nullptr) - lastFetch >= (time_t)settings.sleepSecs;`
  - `src/main.cpp` `setup()`: add `loadSettings();` immediately after `prefs.begin("frame", false);`
  - `src/power.cpp` `goToSleep()`/`quickSleep()`: `uint64_t secs = settings.sleepSecs;` (full quiet-hours handling lands in Task 8; this step only restores compilation)
  - `src/ui.cpp` `drawInfoScreen` "next:" line: `time_t next = time(nullptr) + (time_t)settings.sleepSecs;`

- [ ] **Step 4: Update `src/state.h` doc comment** to list the new keys:

```cpp
#pragma once
#include <Preferences.h>

// Shared persistent state, defined in main.cpp.
// NVS namespace "frame": held / lastEpoch / wifiDesc / tzOff / lastIp
// plus the settings keys (see settings.cpp).
extern Preferences prefs;
extern bool infoVisible; // full-screen info page vs the photo (removed in main rework task)
extern bool held;        // pin/freeze: timer wakes skip fetching
```

- [ ] **Step 5: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS (behavior unchanged: defaults mirror the old constants)

- [ ] **Step 6: Commit**

```bash
git add src/settings.h src/settings.cpp src/config.h src/state.h src/main.cpp src/power.cpp src/ui.cpp
git commit -m "Settings module: NVS-backed runtime config with legacy-identical defaults"
```

---

### Task 6: Fetch URL templating + manual timezone

**Files:**
- Modify: `src/net.cpp` (`imageUrl()`, `syncClock()`)

**Interfaces:**
- Consumes: `settings` (Task 5), `renderUrlTemplate` (Task 2), `epaper` (`display.h`).
- Produces: no signature changes — `fetchImage()`/`syncClock()` keep their `net.h` contracts.

- [ ] **Step 1: Replace `imageUrl()`** (top of `src/net.cpp`; add `#include "settings.h"` and `#include "logic/url_template.h"` to the include block):

```cpp
// The image source is a user-configurable URL template; {seed} defeats
// upstream caches per fetch, {width}/{height} follow the panel rotation.
// Default: weserv re-encode of picsum (embedded decoders need baseline
// JPEG, weserv converts progressive -> baseline at exact panel size).
static String imageUrl() {
    std::string u = renderUrlTemplate(settings.imageUrl.c_str(), esp_random(),
                                      epaper.width(), epaper.height());
    return String(u.c_str());
}
```

- [ ] **Step 2: Manual timezone skips detection.** Replace `syncClock()`:

```cpp
// One NTP sync per wake — the RTC drifts and deep sleep is long, and we're
// online anyway. In manual timezone mode the ip-api call is skipped
// entirely (privacy: no geolocation; also works on offline-only LANs).
bool syncClock() {
    long off = settings.tzAuto ? detectUtcOffset() : prefs.getLong("tzOff", 0);
    configTime(off, 0, "pool.ntp.org");
    struct tm now;
    return getLocalTime(&now, 10000);
}
```

(`detectUtcOffset()` already writes the detected offset to `tzOff`; in manual mode the portal writes `tzOff` instead — Task 9.)

- [ ] **Step 3: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/net.cpp
git commit -m "Fetch uses settings URL template; manual timezone skips ip-api"
```

---

### Task 7: Orientation

**Files:**
- Modify: `src/display.h`, `src/display.cpp` (`applyOrientation`, frame rotation tag)
- Modify: `src/ui.cpp` (center on `width()/height()` instead of hardcoded 600/800)
- Modify: `src/net.cpp` (`showProvisioningScreen` hardcoded coords)
- Modify: `src/main.cpp` (call `applyOrientation()` after `epaper.begin()`)

**Interfaces:**
- Consumes: `settings.rotation` (Task 5).
- Produces: `void applyOrientation()` in `display.h` — sets `epaper.setRotation(settings.rotation)`. `saveFrame()`/`loadFrame()` keep signatures but tag frames with the rotation they were saved under (NVS key `frameRot`); a mismatch makes `loadFrame()` return false ("no saved frame").

- [ ] **Step 1: Add to `src/display.h`** after the `extern EPaper epaper;` line:

```cpp
// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();
```

- [ ] **Step 2: Implement in `src/display.cpp`** (add `#include "settings.h"` and `#include "state.h"` to the includes):

```cpp
void applyOrientation() { epaper.setRotation(settings.rotation); }
```

In `saveFrame()`, after the `written == FRAME_BYTES` check passes, tag the rotation — replace the final `return` with:

```cpp
    if (written != FRAME_BYTES) return false;
    prefs.putUChar("frameRot", settings.rotation);
    return true;
```

In `loadFrame()`, reject frames saved under a different rotation — insert before opening the file:

```cpp
    if (prefs.getUChar("frameRot", DEFAULT_ROTATION) != settings.rotation) {
        Serial.println("frame load: saved under different rotation");
        return false;
    }
```

(add `#include "config.h"` if not present — it already is.)

- [ ] **Step 3: De-hardcode UI coordinates.**
  - `src/ui.cpp` `drawInfoScreen`: replace `int y = 800 - ((n - 1) * lineH) / 2;` with `int y = epaper.height() / 2 - ((n - 1) * lineH) / 2;` and `epaper.drawString(lines[i], 600, y, 4);` with `epaper.drawString(lines[i], epaper.width() / 2, y, 4);`
  - `src/net.cpp` `showProvisioningScreen`: the fixed left margin (20) and font stay; the absolute y-coordinates (40…540) all fit within the shortest dimension (1200), so they work in every rotation — add this comment above the function:

```cpp
// Layout note: all y-coordinates stay under 1200 so the screen renders
// in both portrait (1600 tall) and landscape (1200 tall).
```

- [ ] **Step 4: Apply at boot.** In `src/main.cpp` `setup()`, right after `epaper.begin();` add:

```cpp
    applyOrientation(); // settings.rotation; UI + dither target follow
```

- [ ] **Step 5: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS

- [ ] **Step 6: Commit**

```bash
git add src/display.h src/display.cpp src/ui.cpp src/net.cpp src/main.cpp
git commit -m "Orientation setting: setRotation at boot, rotation-tagged saved frames, resolution-relative UI"
```

---

### Task 8: Quiet-hours wiring

**Files:**
- Modify: `src/power.h`, `src/power.cpp` (`goToSleep`, `quickSleep(uint32_t)`)
- Modify: `src/main.cpp` (timer-wake-inside-window fast path)

**Interfaces:**
- Consumes: `quietAdjustedSleep`, `inQuietWindow`, `secondsUntilQuietEnd` (Task 3); `settings` (Task 5); `CLOCK_SANE_EPOCH` (`config.h`).
- Produces: `void quickSleep(uint32_t secs)` — signature change from `quickSleep()`; `goToSleep()` unchanged externally but quiet-aware. New file-local helper in `power.cpp`: `uint32_t plannedSleepSecs()` (also declared in `power.h` for Task 10's "next:" line).

- [ ] **Step 1: Update `src/power.h`.** Replace the `goToSleep`/`quickSleep` declarations:

```cpp
// Seconds the next sleep will actually last: settings.sleepSecs, extended
// past the quiet-hours window when the wake would land inside it (only
// when the clock is NTP-sane — never trust a 1970 clock).
uint32_t plannedSleepSecs();

// Full deep sleep: panel to low power, enable-line GPIOs latched low,
// timer + any-button wake armed. Quiet-hours aware. Never returns.
void goToSleep();

// Fast re-arm without touching the panel or GPIO holds (held wakes and
// timer wakes landing inside the quiet window). Never returns.
void quickSleep(uint32_t secs);
```

- [ ] **Step 2: Update `src/power.cpp`** (add `#include "settings.h"`, `#include "logic/quiet_hours.h"`, `#include <time.h>` to the includes):

```cpp
static int secondsOfLocalDay(time_t now) {
    struct tm lt;
    localtime_r(&now, &lt);
    return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

uint32_t plannedSleepSecs() {
    time_t now = time(nullptr);
    if (now <= CLOCK_SANE_EPOCH) return settings.sleepSecs;
    return quietAdjustedSleep(secondsOfLocalDay(now), settings.sleepSecs,
                              settings.quietEnabled, settings.quietStartHour,
                              settings.quietEndHour);
}
```

In `goToSleep()` replace `uint64_t secs = settings.sleepSecs;` with `uint64_t secs = plannedSleepSecs();`.

Replace `quickSleep()` with:

```cpp
void quickSleep(uint32_t secs) {
    Serial.printf("nothing to do — back to sleep %u s\n", secs);
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}
```

- [ ] **Step 3: Update `src/main.cpp`.** The held fast path becomes quiet-aware too, and a timer wake that still lands inside the window (clock drift, quiet hours enabled after sleep was armed) goes straight back down. Replace the single line `if (cause == ESP_SLEEP_WAKEUP_TIMER && held) quickSleep(); // no return` with (add `#include "logic/quiet_hours.h"` and `#include <time.h>` to the includes; note `loadSettings()` and `applyUtcOffset(...)` already ran above):

```cpp
    // Fast paths for timer wakes that shouldn't touch the panel: pinned,
    // or the wake landed inside the quiet window. GPIO holds from the
    // previous sleep stay latched, so these must run before hold-release.
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        if (held) quickSleep(plannedSleepSecs()); // no return
        time_t now = time(nullptr);
        if (settings.quietEnabled && now > CLOCK_SANE_EPOCH) {
            struct tm lt;
            localtime_r(&now, &lt);
            int sod = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
            if (inQuietWindow(sod, settings.quietStartHour,
                              settings.quietEndHour))
                quickSleep(secondsUntilQuietEnd(sod, settings.quietStartHour,
                                                settings.quietEndHour)); // no return
        }
    }
```

- [ ] **Step 4: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/power.h src/power.cpp src/main.cpp
git commit -m "Quiet hours: sleep extension at sleep entry, quick re-sleep on in-window timer wakes"
```

---

### Task 9: Portal module (web server + settings form)

**Files:**
- Create: `src/portal.h`, `src/portal.cpp`, `src/portal_html.h`

**Interfaces:**
- Consumes: `settings`/`saveSettings` (Task 5), validators (Task 4), `prefs`/`held` (`state.h`), WiFiManager (`resetSettings`), `WebServer`/`ESPmDNS` (Arduino core).
- Produces:

```cpp
enum class PortalResult { KeyExit, Timeout, Saved, ForgetWifi };
bool startPortal();                              // after Wi-Fi is up: mDNS + HTTP :80
PortalResult runPortal(uint32_t inactivityTimeoutMs); // blocking; stops server before returning
String portalUrl();                              // "http://<name>.local"
```

  All results are handled identically by the caller (exit → fetch cycle → sleep); the enum exists for logging and because ForgetWifi's next fetch cycle will open the provisioning portal.

- [ ] **Step 1: Create `src/portal_html.h`** — the full settings page as a PROGMEM template. `%TOKENS%` are substituted in `portal.cpp`:

```cpp
#pragma once
#include <Arduino.h>

// Single-page settings form. %TOKEN% placeholders are filled by
// buildPage() in portal.cpp. Kept dependency-free: inline CSS, no JS.
inline const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%NAME% — EE02 Frame</title>
<style>
body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
h1{font-size:1.3rem}fieldset{border:1px solid #ccc;border-radius:8px;margin:0 0 1rem;padding:1rem}
legend{font-weight:600;padding:0 .4rem}label{display:block;margin:.6rem 0 .2rem}
input[type=text],select{width:100%;padding:.45rem;border:1px solid #bbb;border-radius:6px;box-sizing:border-box}
.row{display:flex;gap:.8rem}.row>div{flex:1}
button{padding:.55rem 1.2rem;border-radius:6px;border:1px solid #888;background:#f4f4f4;cursor:pointer}
button.primary{background:#2563eb;color:#fff;border-color:#2563eb}
.msg{background:#fee;border:1px solid #c00;border-radius:6px;padding:.6rem;margin-bottom:1rem}
.note{color:#666;font-size:.85rem;margin-top:.2rem}
.danger{border-color:#c00}
</style></head><body>
<h1>%NAME% — settings</h1>
%ERROR%
<form method="POST" action="/save">
<fieldset><legend>Pictures</legend>
<label>Refresh every</label><select name="sleep">%SLEEP_OPTS%</select>
<label>Image source URL</label>
<input type="text" name="url" value="%URL%" maxlength="512">
<div class="note">Must return a baseline (non-progressive) JPEG.
Tokens: {width} {height} {seed}</div>
<label><input type="checkbox" name="paused" %PAUSED%> Paused (pin current picture)</label>
</fieldset>
<fieldset><legend>Quiet hours</legend>
<label><input type="checkbox" name="quiet_en" %QUIET_EN%> Skip refreshes between</label>
<div class="row"><div><select name="quiet_start">%QS_OPTS%</select></div>
<div><select name="quiet_end">%QE_OPTS%</select></div></div>
</fieldset>
<fieldset><legend>Device</legend>
<label>Timezone</label><select name="tz">%TZ_OPTS%</select>
<label>Name (mDNS: <b>%NAME%.local</b>)</label>
<input type="text" name="name" value="%NAME%" maxlength="24">
<div class="note">lowercase letters, digits, hyphens</div>
<label>Orientation</label><select name="rot">%ROT_OPTS%</select>
</fieldset>
<button class="primary" type="submit">Save &amp; apply</button>
</form>
<form method="POST" action="/action/newpic" style="margin-top:1rem">
<button type="submit">Fetch new picture now</button></form>
<form method="POST" action="/action/forgetwifi" style="margin-top:1rem"
onsubmit="return confirm('Forget Wi-Fi and reopen the setup hotspot?')">
<button class="danger" type="submit">Forget Wi-Fi…</button></form>
</body></html>)HTML";

inline const char PORTAL_DONE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EE02 Frame</title>
<style>body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem}</style>
</head><body><h1>%TITLE%</h1><p>%BODY%</p></body></html>)HTML";
```

- [ ] **Step 2: Create `src/portal.h`:**

```cpp
#pragma once
#include <Arduino.h>

// Settings portal, live while the status page is on the panel.
// Lifecycle: connectWifi() -> startPortal() -> runPortal() -> caller runs
// a fetch cycle and sleeps. Every exit path behaves the same; the enum is
// for logging (ForgetWifi's next fetch cycle reopens provisioning).
enum class PortalResult { KeyExit, Timeout, Saved, ForgetWifi };

bool startPortal();                                   // mDNS + HTTP :80
PortalResult runPortal(uint32_t inactivityTimeoutMs); // blocking loop
String portalUrl();                                   // "http://<name>.local"
```

- [ ] **Step 3: Create `src/portal.cpp`:**

```cpp
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

static WebServer server(80);
static PortalResult result;
static bool exitRequested;
static uint32_t lastActivityMs;

String portalUrl() { return "http://" + settings.name + ".local"; }

static String selectOptions(int from, int to, int selected,
                            const char *suffix) {
    String out;
    for (int v = from; v <= to; v++) {
        out += "<option value=\"" + String(v) + "\"" +
               (v == selected ? " selected" : "") + ">" + String(v) + suffix +
               "</option>";
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
        snprintf(label, sizeof(label), "UTC%+ld:%02ld", o / 3600,
                 labs(o % 3600) / 60);
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

static String buildPage(const String &error) {
    String page = FPSTR(PORTAL_HTML);
    page.replace("%ERROR%",
                 error.isEmpty() ? "" : "<div class=\"msg\">" + error + "</div>");
    page.replace("%NAME%", settings.name);
    page.replace("%SLEEP_OPTS%", sleepOptions(settings.sleepSecs));
    page.replace("%URL%", settings.imageUrl);
    page.replace("%PAUSED%", held ? "checked" : "");
    page.replace("%QUIET_EN%", settings.quietEnabled ? "checked" : "");
    page.replace("%QS_OPTS%",
                 selectOptions(0, 23, settings.quietStartHour, ":00"));
    page.replace("%QE_OPTS%",
                 selectOptions(0, 23, settings.quietEndHour, ":00"));
    page.replace("%TZ_OPTS%", tzOptions());
    page.replace("%ROT_OPTS%", rotOptions());
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

    sendDone("Saved", "The frame is applying settings and fetching a "
                      "picture (the panel takes ~30 s to refresh).");
    result = PortalResult::Saved;
    exitRequested = true;
    Serial.println("portal: settings saved");
}

static void handleNewPic() {
    lastActivityMs = millis();
    sendDone("Fetching", "New picture on the way (~30 s panel refresh).");
    result = PortalResult::Saved;
    exitRequested = true;
    Serial.println("portal: new picture requested");
}

static void handleForgetWifi() {
    lastActivityMs = millis();
    sendDone("Wi-Fi forgotten",
             "The frame will reopen the <b>" + String(AP_NAME) +
                 "</b> setup hotspot. Join it to reconnect.");
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
```

- [ ] **Step 4: Move the debounced-press helper where portal + main can share it.** In `src/power.h` add:

```cpp
// Debounced falling-edge press: true once per physical press (blocks
// until release). Shared by the dev-mode loop and the portal loop.
bool buttonPressed(uint8_t pin);
```

In `src/power.cpp` add (this is `pressed()` from `main.cpp`, renamed):

```cpp
bool buttonPressed(uint8_t pin) {
    if (digitalRead(pin) != LOW) return false;
    delay(30);
    if (digitalRead(pin) != LOW) return false;
    while (digitalRead(pin) == LOW) delay(10);
    return true;
}
```

In `src/main.cpp` delete the `static bool pressed(uint8_t pin)` function and change its three uses in `loop()` to `buttonPressed(...)`.

- [ ] **Step 5: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS (portal not yet reachable — wired up in Task 11)

- [ ] **Step 6: Commit**

```bash
git add src/portal.h src/portal.cpp src/portal_html.h src/power.h src/power.cpp src/main.cpp
git commit -m "Settings portal: embedded web form, validation-gated save, state/actions mirror"
```

---

### Task 10: Status page UI (QR + legend + portal URL)

**Files:**
- Modify: `src/ui.h`, `src/ui.cpp` (rename `drawInfoScreen` → `drawStatusScreen`, add URL/IP/QR/legend)

**Interfaces:**
- Consumes: `portalUrl()` (Task 9), `plannedSleepSecs()` (Task 8), `settings` (Task 5), `qrcode.h` (ricmoo/QRCode, dep added in Task 1), `prefs` key `lastIp` (written in Task 11).
- Produces: `void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta)` — draws into the sprite only; caller calls `epaper.update()`. Replaces `drawInfoScreen` (update the declaration + doc comment in `ui.h`; the old name must not survive).

- [ ] **Step 1: Update `src/ui.h`.** Replace the `drawInfoScreen` declaration and comment with:

```cpp
// Full-screen status page: wake/battery/wifi/refresh info, the settings
// portal URL + QR code, and a button legend with live state. Draws into
// the sprite only; the caller starts the portal and calls epaper.update().
void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta);
```

- [ ] **Step 2: Rewrite the drawing code in `src/ui.cpp`.** Add includes `#include "portal.h"`, `#include "power.h"`, `#include "settings.h"`, `#include <qrcode.h>`. Replace `drawInfoScreen` with:

```cpp
// QR code for the portal URL. Version 4 (33x33 modules) fits
// "http://<24-char-name>.local" at ECC_LOW with room to spare.
static void drawQr(const String &text, int cx, int cy, int scale) {
    QRCode qr;
    uint8_t data[qrcode_getBufferSize(4)];
    if (qrcode_initText(&qr, data, 4, ECC_LOW, text.c_str()) != 0) return;
    const int px = qr.size * scale;
    const int x0 = cx - px / 2, y0 = cy - px / 2;
    // Quiet zone: 4 modules of white on every side.
    epaper.fillRect(x0 - 4 * scale, y0 - 4 * scale, px + 8 * scale,
                    px + 8 * scale, TFT_WHITE);
    for (int y = 0; y < qr.size; y++)
        for (int x = 0; x < qr.size; x++)
            if (qrcode_getModule(&qr, x, y))
                epaper.fillRect(x0 + x * scale, y0 + y * scale, scale, scale,
                                TFT_BLACK);
}

void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    // Metadata comes from recordFetchMetadata(); this page only reads it.
    time_t lastEpoch = (time_t)prefs.getULong("lastEpoch", 0);
    String wifiDesc = prefs.getString("wifiDesc", "?");
    String lastIp = prefs.getString("lastIp", "");

    String lines[6];
    int n = 0;
    lines[n++] = settings.name + "  —  wake: " + String(wakeReason());

    if (lastEpoch > CLOCK_SANE_EPOCH) {
        struct tm lastTm;
        localtime_r(&lastEpoch, &lastTm);
        char dow[16], hm[8];
        strftime(dow, sizeof(dow), "%a %b", &lastTm);
        strftime(hm, sizeof(hm), "%H:%M", &lastTm);
        lines[n++] = "last: " + String(dow) + " " + String(lastTm.tm_mday) +
                     " " + String(hm);
    } else {
        lines[n++] = "last: --";
    }

    if (held) {
        lines[n++] = "next: pinned";
    } else {
        time_t next = time(nullptr) + (time_t)plannedSleepSecs();
        struct tm nextTm;
        localtime_r(&next, &nextTm);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &nextTm);
        lines[n++] = "next: " + String(hm);
    }

    lines[n++] = "wifi: " + wifiDesc;

    String batt = "batt: " + String(batteryPercent(vbatMv)) + "% (" +
                  String(vbatMv / 1000.0f, 2) + "V";
    if (haveDelta) {
        batt += ", ";
        if (deltaMv >= 0) batt += "+";
        batt += String(deltaMv) + "mV";
        if (deltaMv >= 20) batt += ", chg";
    }
    batt += ")";
    lines[n++] = batt;

    const int cx = epaper.width() / 2;
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextSize(2);
    const int lineH = 90;
    // Info block in the upper half; QR + legend below it.
    int y = epaper.height() / 4 - ((n - 1) * lineH) / 2 + 60;
    for (int i = 0; i < n; i++, y += lineH)
        epaper.drawString(lines[i], cx, y, 4);

    // Portal URL at base text size (a full URL + IP overflows size 2),
    // then the QR code below it.
    epaper.setTextSize(1);
    epaper.drawString("settings: " + portalUrl() +
                          (lastIp.isEmpty() ? "" : "  (" + lastIp + ")"),
                      cx, y, 4);
    drawQr(portalUrl(), cx, y + 220, 8); // ~33*8=264 px + quiet zone
    epaper.setTextSize(2);

    // Button legend with live state — the frame documents itself.
    epaper.setTextSize(1);
    const int legendTop = epaper.height() - 200;
    epaper.drawString("KEY1: back to photo (settings portal is on while "
                      "this page shows)", cx, legendTop, 4);
    epaper.drawString("KEY2: new picture now", cx, legendTop + 55, 4);
    epaper.drawString(held ? "KEY3: unpin — currently pinned"
                           : "KEY3: pin current picture",
                      cx, legendTop + 110, 4);
    epaper.setTextDatum(TL_DATUM);
}
```

- [ ] **Step 3: Fix the call site so the build stays green.** In `src/main.cpp` `handleToggleWake`, change `drawInfoScreen(vbatMv, deltaMv, haveDelta);` to `drawStatusScreen(vbatMv, deltaMv, haveDelta);` (full dispatch rework is Task 11; this keeps every commit compiling).

- [ ] **Step 4: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "Status page: portal URL + QR code + live button legend"
```

---

### Task 11: Main dispatch rework (status mode, no info state, no power-on gesture)

**Files:**
- Modify: `src/main.cpp` (full rewrite below)
- Modify: `src/state.h` (drop `infoVisible`)
- Modify: `src/net.cpp` (record `lastIp` on connect)

**Interfaces:**
- Consumes: everything above — `runPortal`/`startPortal` (Task 9), `drawStatusScreen` (Task 10), `plannedSleepSecs`/`quickSleep(uint32_t)`/`buttonPressed` (Tasks 8/9), `loadSettings`/`applyOrientation` (Tasks 5/7).
- Produces: final button semantics — KEY1 status page + portal session, KEY2 fetch, KEY3 pin. NVS key `info` is no longer read or written; the KEY2-at-power-on forget-Wi-Fi gesture is gone (portal action replaces it). New NVS key `lastIp` (last STA IP, shown on the status page).

- [ ] **Step 1: Record the IP whenever Wi-Fi connects.** In `src/net.cpp` `connectWifi()`, inside the `if (ok)` branch, after the `Serial.printf("connected …")` line, add:

```cpp
        prefs.putString("lastIp", WiFi.localIP().toString());
```

(add `#include "state.h"` — already included.)

- [ ] **Step 2: Drop `infoVisible` from `src/state.h`:**

```cpp
#pragma once
#include <Preferences.h>

// Shared persistent state, defined in main.cpp.
// NVS namespace "frame": held / lastEpoch / wifiDesc / tzOff / lastIp /
// frameRot, plus the settings keys (see settings.cpp).
extern Preferences prefs;
extern bool held; // pin/freeze: timer wakes skip fetching
```

- [ ] **Step 3: Rewrite `src/main.cpp`:**

```cpp
// EE02 e-paper photo frame — wake, act, deep-sleep. See README.md.
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include "driver/gpio.h"
#include <time.h>

#include "config.h"
#include "display.h"
#include "logic/quiet_hours.h"
#include "net.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "ui.h"

Preferences prefs; // NVS namespace "frame"
bool held = false;

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR int32_t lastVbatMv = -1; // survives deep sleep, not reset/flash

// Read the battery and compute the wake-to-wake delta (RTC-persisted).
static int32_t readBatteryWithDelta(int32_t &deltaMv, bool &haveDelta) {
    int32_t vbatMv = readBatteryMv();
    haveDelta = lastVbatMv >= 0;
    deltaMv = haveDelta ? vbatMv - lastVbatMv : 0;
    lastVbatMv = vbatMv;
    if (haveDelta)
        Serial.printf("battery: %.2f V ~%d%% (%+d mV since last wake)\n",
                      vbatMv / 1000.0f, batteryPercent(vbatMv), (int)deltaMv);
    else
        Serial.printf("battery: %.2f V ~%d%%\n",
                      vbatMv / 1000.0f, batteryPercent(vbatMv));
    return vbatMv;
}

// Fetch a new photo, dither it, persist it, and show it full-bleed.
static void doFetchCycle() {
    if (!connectWifi()) {
        showError("wifi setup failed or timed out");
    } else if (fetchImage()) {
        syncClock();
        saveFrame();
        recordFetchMetadata();
        Serial.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        Serial.println("done");
    }
}

// KEY1: status page + settings portal. Draw first (from NVS cache, no
// network), then bring Wi-Fi + the portal up — by the time the panel
// finishes its ~30 s refresh and a phone is out, the portal is live.
// Every exit path (KEY1 again, save, timeout, forget-wifi) falls through
// to a fetch cycle so changes take effect visibly.
static void runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    drawStatusScreen(vbatMv, deltaMv, haveDelta);
    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done");
    if (!connectWifi()) return; // provisioning fallback already drew
    if (!startPortal()) return;
    PortalResult r = runPortal(10 * 60 * 1000UL);
    switch (r) {
        case PortalResult::KeyExit: Serial.println("portal: KEY1 exit"); break;
        case PortalResult::Timeout: Serial.println("portal: idle timeout"); break;
        case PortalResult::Saved: break;      // logged in the handler
        case PortalResult::ForgetWifi: break; // next connect reopens provisioning
    }
    // Settings (rotation, url, ...) may have changed: reapply orientation
    // before the fetch redraws the panel.
    applyOrientation();
}

// KEY3: flip pin/freeze. LED feedback only — the photo stays up.
static void togglePin() {
    held = !held;
    prefs.putBool("held", held);
    Serial.printf("held now %s\n", held ? "on" : "off");
    blinkLed(held ? 2 : 1);
}

void setup() {
    // Instant acknowledgment: blink before anything else so a button press
    // gets feedback in ~0.5 s (the panel itself takes ~30 s to change).
    // 1 blink = new picture, 2 = status page, 3 = pin/freeze.
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t ackBits = esp_sleep_get_ext1_wakeup_status();
        int n = (ackBits & (1ULL << BTN_NEW_PIC)) ? 1
              : (ackBits & (1ULL << BTN_INFO))    ? 2
              : (ackBits & (1ULL << BTN_PIN))     ? 3 : 0;
        blinkLed(n, 80); // fast ack: even 3 blinks finish in ~480 ms
    }

    Serial.begin(115200);
    // USB-CDC needs ~2 s before prints are visible — only worth paying on a
    // cold start (bench/debug). Button and timer wakes get a token delay.
    delay(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED ? 2000
                                                                     : 200);
    bootCount++;
    Serial.printf("ee02-frame: boot #%u, wake: %s\n", bootCount, wakeReason());

    prefs.begin("frame", false);
    loadSettings();
    held = prefs.getBool("held", false);
    // TZ env doesn't survive deep sleep: without this, times rendered on
    // non-fetch wakes come out as UTC. Fetch wakes overwrite it with a
    // freshly detected (or manual) offset in syncClock().
    applyUtcOffset(prefs.getLong("tzOff", 0));

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t btnBits = (cause == ESP_SLEEP_WAKEUP_EXT1)
                           ? esp_sleep_get_ext1_wakeup_status() : 0;

    // Fast paths for timer wakes that shouldn't touch the panel: pinned,
    // or the wake landed inside the quiet window. GPIO holds from the
    // previous sleep stay latched, so these must run before hold-release.
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        if (held) quickSleep(plannedSleepSecs()); // no return
        time_t now = time(nullptr);
        if (settings.quietEnabled && now > CLOCK_SANE_EPOCH) {
            struct tm lt;
            localtime_r(&now, &lt);
            int sod = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
            if (inQuietWindow(sod, settings.quietStartHour,
                              settings.quietEndHour))
                quickSleep(secondsUntilQuietEnd(
                    sod, settings.quietStartHour,
                    settings.quietEndHour)); // no return
        }
    }

    // Release the pin holds from the previous deep sleep (no-op on first
    // boot) so the panel and battery divider can be driven again.
    gpio_hold_dis((gpio_num_t)EPAPER_EN_PIN);
    gpio_hold_dis((gpio_num_t)BATTERY_EN_PIN);

    pinMode(BTN_NEW_PIC, INPUT); // external pull-ups on board
    pinMode(BTN_INFO, INPUT);    // polled by loop() in dev mode
    pinMode(BTN_PIN, INPUT);
    digitalWrite(LED_PIN, LOW); // LED on while awake

    int32_t deltaMv;
    bool haveDelta;
    int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);

    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

    epaper.begin();
    applyOrientation(); // settings.rotation; UI + dither target follow

    if (btnBits & (1ULL << BTN_PIN)) {
        togglePin(); // photo stays up; no fetch, no panel touch
    } else if (btnBits & (1ULL << BTN_INFO)) {
        runStatusMode(vbatMv, deltaMv, haveDelta);
        doFetchCycle(); // every portal exit shows a fresh photo
    } else {
        doFetchCycle(); // power-on / btn-new-pic / timer
    }

    digitalWrite(LED_PIN, HIGH);
    maybeSleep(); // deep sleep — or return, in dev mode, and run loop()
}

// Only runs in dev mode (USB host attached): the port stays up for
// instant flashing, buttons are polled instead of EXT1-woken, and the
// configured photo cadence still applies. Host gone -> normal deep sleep.
void loop() {
    if (!usbHostPresent()) {
        Serial.println("usb host gone — leaving dev mode");
        goToSleep(); // never returns
    }

    bool info = buttonPressed(BTN_INFO);
    bool pin = !info && buttonPressed(BTN_PIN);
    bool newPic = !info && !pin && buttonPressed(BTN_NEW_PIC);

    bool fetchDue = false;
    if (!held) {
        time_t lastFetch = (time_t)prefs.getULong("lastEpoch", 0);
        fetchDue = time(nullptr) - lastFetch >= (time_t)settings.sleepSecs;
    }

    if (pin) {
        togglePin();
    } else if (info || newPic || fetchDue) {
        digitalWrite(LED_PIN, LOW);
        int32_t deltaMv;
        bool haveDelta;
        int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);
        if (info) runStatusMode(vbatMv, deltaMv, haveDelta);
        doFetchCycle();
        digitalWrite(LED_PIN, HIGH);
    }

    delay(50);
}
```

- [ ] **Step 4: Build + tests**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS

- [ ] **Step 5: Clean up dead NVS state.** Confirm nothing references the `info` key anymore:

Run: `grep -rn '"info"' src/`
Expected: no matches

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/state.h src/net.cpp
git commit -m "Main rework: KEY1 status page + portal session, drop info state and power-on wifi-forget gesture"
```

---

### Task 12: README overhaul + hardware verification checklist

**Files:**
- Modify: `README.md` (full replacement below)
- Create: `docs/hardware-checklist.md`

**Interfaces:**
- Consumes: final behavior from Tasks 1–11.
- Produces: user-facing docs. Photo placeholders are intentional — Sven fills them; they are the only permitted "placeholder" in this plan.

- [ ] **Step 1: Replace `README.md` with:**

````markdown
# ee02-frame

[![ci](../../actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)

Firmware for the **Seeed Studio XIAO ePaper Display Board EE02** (XIAO
ESP32-S3 Plus) driving a **13.3″ Spectra 6 e-ink panel** (1200×1600, six
colors) as a battery-powered photo frame.

<!-- PHOTO: frame on a wall showing a photo -->
<!-- PHOTO: status page with QR code -->

On a schedule you choose it wakes from deep sleep, fetches a photo from a
URL you choose, dithers it to the panel's six colors, refreshes, and goes
back to sleep. Everything is configured **on the device itself** — no
account, no cloud, no companion server. This is a hobby project: support
is best-effort, MIT-licensed, no warranty.

## Supported hardware

Exactly one combo: the EE02 board with the 13.3″ Spectra 6 panel
(`BOARD_SCREEN_COMBO=510` in `platformio.ini`). Other XIAO ePaper combos
need changes to `platformio.ini` and `src/config.h` and are untested.

| Part | Source |
|---|---|
| XIAO ePaper Display Board EE02 (XIAO ESP32-S3 Plus) | Seeed Studio |
| 13.3″ Spectra 6 e-paper panel, 1200×1600 | Seeed Studio |
| 3.7 V Li-ion battery, JST 1.25 | any (see battery notes) |

## Buttons

| Silkscreen | GPIO | Function |
|---|---|---|
| KEY1 | 2 | Toggle the full-screen **status page**. While it shows, the **settings portal** is live on your Wi-Fi. |
| KEY2 | 3 | Fetch a **new picture** now. |
| KEY3 | 5 | **Pin/freeze** the current photo — scheduled refreshes pause (and barely sip battery) until pressed again. |
| RESET | — | Hardware reboot (clears boot counter and battery-delta memory). With BOOT held: flashing bootloader. |

Every press is acknowledged by the LED within ~0.5 s (1 blink = new
picture, 2 = status page, 3 = pin), because the panel itself takes
~25–30 s to change — that's Spectra 6 physics, and the panel has **no
partial-refresh mode** (color e-ink waveforms are full-panel only).

## First-time setup

1. Connect the panel's FPC cable, flip the power switch on, plug in USB-C.
2. The panel shows Wi-Fi instructions: join the `EE02-Setup` hotspot from
   a phone, open `http://192.168.4.1`, pick your **2.4 GHz** network.
3. Credentials persist on-device (NVS). Nothing secret ever enters this repo.

## Settings

Press **KEY1**. The panel shows the status page (wake reason, last/next
refresh, Wi-Fi, battery) plus a URL and QR code — open it from any device
on the same Wi-Fi:

| Setting | Choices | Default |
|---|---|---|
| Refresh interval | 15 min – 24 h | 1 h |
| Image source URL | any http(s) URL template | picsum via weserv |
| Paused | pin/freeze (same as KEY3) | off |
| Quiet hours | skip refreshes in a nightly window | off |
| Timezone | auto (IP geolocation) or manual offset | auto |
| Device name | mDNS hostname (`<name>.local`) | `ee02` |
| Orientation | portrait / landscape, each flippable | portrait |

The page also has **Fetch new picture now** and **Forget Wi-Fi** buttons.
The portal stops when you leave the status page (press KEY1 again, save,
or 10 minutes idle) — the device then fetches a picture and sleeps.
If saved Wi-Fi stops working (new router, moved house), the frame
reopens the `EE02-Setup` hotspot by itself.

### Image source contract

The URL must return a **baseline (non-progressive) JPEG** at the panel
size. Tokens are substituted per fetch:

- `{width}` / `{height}` — panel size after orientation (1200×1600
  portrait, 1600×1200 landscape)
- `{seed}` — random number (cache-buster)

The default goes through images.weserv.nl because picsum serves
progressive JPEGs, which the on-device decoder can't parse; weserv
re-encodes to baseline at exact size. Pointing at your own server or a
static file works — just honor the contract. Please be a good citizen of
free services: one frame is nothing, a fleet is not.

## Privacy & security notes

- Image fetches use HTTPS **without certificate validation**
  (`setInsecure()`): an attacker on your network path could substitute
  the image. Accepted trade-off for a photo frame; noted for transparency.
- Timezone auto-detect calls `ip-api.com` over plain HTTP (their free,
  **non-commercial** tier) and reveals your public IP to them. Set a
  manual timezone in the portal to disable it entirely.
- The settings portal is HTTP on your LAN, unauthenticated — anyone on
  your Wi-Fi can change your photo frame's settings. Threat model: roommates.

## Building & flashing

PlatformIO CLI. The display driver is selected entirely by the two
`build_flags` in `platformio.ini` — never edit library files.

```bash
pio run -e ee02      # build firmware
pio run -t upload    # flash over USB-C
pio device monitor   # serial at 115200 (USB-CDC)
pio test -e native   # host-side unit tests (no hardware needed)
```

**Dev mode:** plugged into a computer, the board never sleeps —
`pio run -t upload` just works. A USB *host* is detected via the
USB-Serial-JTAG SOF frame counter, so chargers never trigger dev mode.
If it was last running on battery/charger (asleep, USB port gone), wake
it first: press any user button and run the upload within the wake window
(a port-watching loop works well: `until ls /dev/cu.usbmodem* 2>/dev/null;
do sleep 0.2; done; pio run -t upload`), wait for the scheduled self-wake,
or hold **BOOT**, tap **RESET**, release BOOT — then flash and press
RESET after.

## Source layout

```
src/config.h      pins, buttons, defaults, constants
src/settings.*    runtime configuration (NVS-backed)
src/logic/        pure decision logic — host-testable, no Arduino deps
src/display.*     EPaper object, 6-color dither, JPEG decode, frame save/load
src/net.*         Wi-Fi provisioning (WiFiManager), photo fetch, timezone+NTP
src/portal.*      settings web portal (served while the status page shows)
src/power.*       battery ADC + percent curve, LED, deep-sleep entry
src/ui.*          wake reason, status page, fetch metadata
src/main.cpp      wake dispatch: which wake does what
test/             native unit tests (pio test -e native)
```

## Hardware notes (hard-won)

- **The 4 bpp framebuffer is palette-indexed.** `TFT_*` color macros are
  panel nibbles (WHITE=0x0, GREEN=0x2, RED=0x6, YELLOW=0xB, BLUE=0xD,
  BLACK=0xF) and `drawPixel` stores `color & 0x0F`. Raw RGB565 pushed via
  `pushImage` renders garbage — photos must be dithered to the palette.
- **Deep sleep floats digital-only pads** — the panel/battery enable lines
  (GPIO43/6) are latched with `gpio_hold_en` before sleeping, released at
  boot. The held/quiet fast path (`quickSleep`) deliberately leaves them
  latched.
- **The TZ environment doesn't survive deep sleep** — it's reapplied from
  the NVS-cached offset at every boot, or non-fetch wakes render UTC.
- **Battery**: charged from USB-C through the board's BQ24070 (~300 mA
  fast charge, ~30 mA precharge below 3 V). Its **8-hour safety timer**
  latches a fault on deeply discharged large packs — both charge LEDs go
  dark and charging stops; unplug/replug USB to resume. The battery
  percent shown is a discharge-curve estimate, not a fuel gauge.
- ADC reads use `analogReadMilliVolts` (eFuse-calibrated); the naive
  `raw/4095*3.3` conversion reads 20–30 % low on the S3.

## License

MIT — see [LICENSE](LICENSE).
````

- [ ] **Step 2: Create `docs/hardware-checklist.md`:**

```markdown
# On-hardware verification checklist

Manual pass on a real EE02 before tagging a release. Monitor at 115200.

## Portal basics
- [ ] KEY1 from sleep: 2 ack blinks, status page draws (URL, IP, QR, legend)
- [ ] QR code scans from a phone and opens the portal
- [ ] `http://<name>.local` and the numeric IP both serve the form
- [ ] Form shows current values; Save with a bad name shows the error and changes nothing (power-cycle → old values)
- [ ] Valid Save: confirmation page, panel fetches a new photo, device sleeps
- [ ] KEY1 while portal is up: exits, fetches, sleeps
- [ ] 10-minute idle: same exit path (check serial log for "idle timeout")

## Settings behavior
- [ ] Interval 15 min: next timer wake ~15 min later (serial timestamps)
- [ ] Orientation landscape: photo fetches at 1600×1200 and renders correctly; status page centered; saved-frame reload after RESET shows "different rotation" and refetches
- [ ] Quiet hours spanning now: sleep log shows extended sleep to window end
- [ ] Manual timezone: fetch log shows no ip-api call; times correct
- [ ] Paused checkbox: equals KEY3 (timer wakes take the quick-sleep path)
- [ ] Fetch-new-picture button: new photo appears
- [ ] Forget Wi-Fi: EE02-Setup hotspot reopens with instructions

## Regressions
- [ ] KEY2: 1 blink, new photo
- [ ] KEY3: 2/1 blinks, pin/unpin; pinned timer wake stays asleep (log)
- [ ] Cold boot with no Wi-Fi saved: provisioning screen + hotspot works
- [ ] Dev mode: plugged into a computer — stays awake, buttons polled, KEY1 portal session works, unplug → sleeps
- [ ] Battery wake after unplugging: sleeps normally (no dev-mode leak)
```

- [ ] **Step 3: Build + tests one final time**

Run: `pio run -e ee02 && pio test -e native`
Expected: both SUCCESS

- [ ] **Step 4: Commit**

```bash
git add README.md docs/hardware-checklist.md
git commit -m "README: settings portal, image-source contract, security notes; add hardware checklist"
```

---

## Post-merge (not tasks — owner actions)

- Run `docs/hardware-checklist.md` on the physical frame.
- Take the two README photos and replace the `<!-- PHOTO -->` comments.
- Rename the GitHub repo to `ee02-frame`, add description + topics
  (`esp32-s3`, `epaper`, `spectra-6`, `platformio`, `xiao`, `photo-frame`).
- Decide on commit email exposure (GitHub noreply going forward, or accept).
- Flip visibility to public, then tag `v1.0.0`.
