# Debug Visibility (RAM Log + Cached Photo) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a RAM ring-buffer debug log and a flash-cached last photo, both reachable over HTTP from the existing settings portal, and stop KEY1 (status view) from forcing a new randomized photo fetch on every press.

**Architecture:** Three independently-scoped pieces glued together at the portal and `runStatusMode()`: `RingLog` (pure logic, host-testable) backs `DevLog`, a `Print`-derived tee that every existing `Serial.print*` call site is renamed to use. `PhotoCache` wraps `LittleFS` on the existing unused `spiffs` partition to persist the last fetched JPEG. Three new portal routes (`/last.jpg`, `/log`, `/debug`) expose both. `runStatusMode()`'s exit path is rebuilt to redisplay the cached photo on a no-op view (`KeyExit`/`Timeout`) instead of always re-fetching, while a real settings change (`Saved`/`ForgetWifi`) still triggers a real fetch as today.

**Tech Stack:** Arduino/ESP32 (PlatformIO, `framework = arduino`), `LittleFS` (ships with `framework-arduinoespressif32`, no new `lib_deps`), Unity for native host tests.

## Global Constraints

- No partition table changes — `LittleFS` mounts on the existing `spiffs`-labeled partition (`default_16MB.csv`, 0x360000 bytes / ~3.4MB).
- No new `lib_deps` in `platformio.ini`.
- `DevLog` ring buffer capacity: 8192 bytes (`DEVLOG_CAPACITY` in `src/devlog.cpp`), RAM-only, does not survive deep sleep.
- Cache format is the original downloaded JPEG bytes at `/last.jpg` on `LittleFS` — never a per-panel dithered framebuffer.
- Every renamed `Serial.print*` call site must still reach the real hardware serial port (so `pio device monitor` from a real terminal is unaffected) — `DevLog::write()` forwards to `Serial` before appending to the ring buffer.
- `Serial.begin(...)` (`main.cpp:118`) and `Serial.flush()` (`power.cpp:117,133`) stay calling the real `Serial` object — `Print`/`DevLog` has no `begin()`/`flush()`.
- New portal routes (`/last.jpg`, `/log`, `/debug`) get no auth — same unauthenticated-on-your-LAN threat model the README already documents for the rest of the portal.

---

### Task 1: `RingLog` pure ring-buffer logic + native tests

**Files:**
- Create: `src/logic/ring_log.h`
- Test: `test/test_ring_log/main.cpp`

**Interfaces:**
- Produces: `class RingLog { public: explicit RingLog(size_t capacity); void append(const uint8_t *data, size_t len); std::string snapshot() const; size_t capacity() const; };` — pure C++, no Arduino deps, following this repo's `src/logic/*.h` convention (see `src/logic/quiet_hours.h`).

- [ ] **Step 1: Write the failing test**

Create `test/test_ring_log/main.cpp`:

```cpp
#include <unity.h>
#include "logic/ring_log.h"

void setUp() {}
void tearDown() {}

void test_empty_snapshot() {
    RingLog log(8);
    TEST_ASSERT_EQUAL_STRING("", log.snapshot().c_str());
}

void test_snapshot_before_wrap() {
    RingLog log(8);
    log.append((const uint8_t *)"abc", 3);
    TEST_ASSERT_EQUAL_STRING("abc", log.snapshot().c_str());
}

void test_snapshot_after_wrap_keeps_chronological_order() {
    RingLog log(4);
    log.append((const uint8_t *)"abcdef", 6); // wraps: only "cdef" fits
    TEST_ASSERT_EQUAL_STRING("cdef", log.snapshot().c_str());
}

void test_multiple_appends_wrap_correctly() {
    RingLog log(4);
    log.append((const uint8_t *)"ab", 2);
    log.append((const uint8_t *)"cd", 2); // buffer now full: "abcd"
    log.append((const uint8_t *)"ef", 2); // overwrites oldest 2 -> "cdef"
    TEST_ASSERT_EQUAL_STRING("cdef", log.snapshot().c_str());
}

void test_single_write_larger_than_capacity_keeps_tail() {
    RingLog log(3);
    log.append((const uint8_t *)"hello", 5); // only the last 3 bytes matter
    TEST_ASSERT_EQUAL_STRING("llo", log.snapshot().c_str());
}

void test_capacity_reports_constructor_value() {
    RingLog log(4096);
    TEST_ASSERT_EQUAL_UINT32(4096, (uint32_t)log.capacity());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_snapshot);
    RUN_TEST(test_snapshot_before_wrap);
    RUN_TEST(test_snapshot_after_wrap_keeps_chronological_order);
    RUN_TEST(test_multiple_appends_wrap_correctly);
    RUN_TEST(test_single_write_larger_than_capacity_keeps_tail);
    RUN_TEST(test_capacity_reports_constructor_value);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_ring_log`
Expected: FAIL to compile — `logic/ring_log.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `src/logic/ring_log.h`:

```cpp
#pragma once
// Fixed-capacity byte ring buffer for a rolling debug log: appends
// overwrite the oldest bytes once full; snapshot() returns bytes in
// chronological (oldest -> newest) order. Pure logic: host-testable, no
// Arduino deps -- same pattern as quiet_hours.h / battery_curve.h.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class RingLog {
public:
    explicit RingLog(size_t capacity)
        : cap_(capacity), buf_(capacity ? new uint8_t[capacity] : nullptr) {}
    ~RingLog() { delete[] buf_; }
    RingLog(const RingLog &) = delete;
    RingLog &operator=(const RingLog &) = delete;

    void append(const uint8_t *data, size_t len) {
        if (cap_ == 0 || len == 0) return;
        if (len >= cap_) {
            // Only the tail of this one write fits -- start the ring
            // fresh from offset 0 with exactly the last cap_ bytes.
            memcpy(buf_, data + (len - cap_), cap_);
            head_ = 0;
            filled_ = true;
            return;
        }
        size_t firstPart = cap_ - head_;
        if (firstPart >= len) {
            memcpy(buf_ + head_, data, len);
        } else {
            memcpy(buf_ + head_, data, firstPart);
            memcpy(buf_, data + firstPart, len - firstPart);
        }
        size_t newHead = head_ + len;
        if (newHead >= cap_) filled_ = true;
        head_ = newHead % cap_;
    }

    std::string snapshot() const {
        if (!filled_)
            return std::string(reinterpret_cast<const char *>(buf_), head_);
        std::string out;
        out.reserve(cap_);
        out.append(reinterpret_cast<const char *>(buf_ + head_), cap_ - head_);
        out.append(reinterpret_cast<const char *>(buf_), head_);
        return out;
    }

    size_t capacity() const { return cap_; }

private:
    size_t cap_;
    uint8_t *buf_;
    size_t head_ = 0;
    bool filled_ = false;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_ring_log`
Expected: PASS, all 6 assertions green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/ring_log.h test/test_ring_log/main.cpp
git commit -m "Add RingLog: pure ring-buffer logic for the debug log"
```

---

### Task 2: `DevLog` Arduino tee wrapper

**Files:**
- Create: `src/devlog.h`
- Create: `src/devlog.cpp`

**Interfaces:**
- Consumes: `RingLog` from Task 1 (`#include "logic/ring_log.h"`).
- Produces: `class DevLog : public Print { public: size_t write(uint8_t b) override; size_t write(const uint8_t *buffer, size_t size) override; String snapshot() const; };` and `extern DevLog devLog;` — the global instance every later task's renamed call sites and the `/log`/`/debug` routes use.

- [ ] **Step 1: Write `src/devlog.h`**

```cpp
#pragma once
#include <Arduino.h>

// Debug log reachable over HTTP without an interactive serial-monitor
// TTY (see docs/superpowers/specs/2026-08-13-debug-visibility-design.md).
// Every Serial.print/println/printf call site in this firmware goes
// through devLog instead: it forwards to the real Serial unchanged (so
// `pio device monitor` from a real terminal is unaffected), and
// additionally keeps a rolling RAM copy that /log and /debug read back
// over HTTP. RAM-only -- does not survive deep sleep. That's intentional:
// it's only ever read while the portal is reachable, which in dev mode
// means the board is already staying awake for the whole session.
class DevLog : public Print {
public:
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    // Buffered output so far, oldest -> newest.
    String snapshot() const;
};

extern DevLog devLog;
```

- [ ] **Step 2: Write `src/devlog.cpp`**

```cpp
#include "devlog.h"
#include "logic/ring_log.h"

namespace {
constexpr size_t DEVLOG_CAPACITY = 8192;
RingLog ring(DEVLOG_CAPACITY);
} // namespace

DevLog devLog;

size_t DevLog::write(uint8_t b) { return write(&b, 1); }

size_t DevLog::write(const uint8_t *buffer, size_t size) {
    size_t written = Serial.write(buffer, size);
    ring.append(buffer, size);
    return written;
}

String DevLog::snapshot() const {
    std::string s = ring.snapshot();
    String out;
    out.reserve(s.size());
    out.concat(s.data(), s.size());
    return out;
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS — `devlog.cpp`/`devlog.h` compile cleanly (nothing references `devLog` yet, so this only checks the new files themselves build).

- [ ] **Step 4: Commit**

```bash
git add src/devlog.h src/devlog.cpp
git commit -m "Add DevLog: Print tee forwarding Serial output into RingLog"
```

---

### Task 3: Rename all `Serial.print*` call sites to `devLog`

**Files:**
- Modify: `src/main.cpp` (14 call sites, lines 30,33,47,54,60,62,75,79,84,85,100,124,193,203)
- Modify: `src/power.cpp` (3 call sites, lines 25,116,132 — NOT lines 117,133, which are `Serial.flush()`)
- Modify: `src/net.cpp` (9 call sites, lines 103,105,109,132,135,140,164,223,232)
- Modify: `src/portal.cpp` (5 call sites, lines 188,196,208,214,226)
- Modify: `src/display.cpp` (6 call sites, lines 170,174,218,227,231,237)

**Interfaces:**
- Consumes: `devLog` global from Task 2 (`#include "devlog.h"`).

- [ ] **Step 1: Add the include to each of the 5 files**

All 5 files already have `#include "display.h"` as an exact, standalone
line — insert the new include directly after it in each:

```bash
cd /Users/sven/Developer/hello-epaper
sed -i '' 's/^#include "display.h"$/&\n#include "devlog.h"/' \
  src/main.cpp src/power.cpp src/net.cpp src/portal.cpp src/display.cpp
```

Expected: each file now has `#include "devlog.h"` on its own line right
after `#include "display.h"`. Verify: `grep -c '#include "devlog.h"' src/main.cpp src/power.cpp src/net.cpp src/portal.cpp src/display.cpp` should print `1` for all five.

- [ ] **Step 2: Rename `Serial.` to `devLog.` in bulk, then revert the 3 exceptions**

```bash
sed -i '' 's/\bSerial\./devLog./g' src/main.cpp src/power.cpp src/net.cpp src/portal.cpp src/display.cpp
sed -i '' 's/devLog\.begin(115200)/Serial.begin(115200)/' src/main.cpp
sed -i '' 's/devLog\.flush()/Serial.flush()/g' src/power.cpp
```

- [ ] **Step 3: Verify no stray renames and no missed call sites**

```bash
grep -rn "devLog\.\(begin\|flush\)" src/ && echo "FAIL: exception not reverted" || echo "ok: exceptions reverted"
grep -rn "Serial\.\(print\|write\)" src/ && echo "FAIL: missed a call site" || echo "ok: all renamed"
```

Expected: both print "ok: ...". If either prints FAIL, fix the offending line before continuing.

- [ ] **Step 4: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/power.cpp src/net.cpp src/portal.cpp src/display.cpp
git commit -m "Route all Serial.print* call sites through devLog"
```

---

### Task 4: `PhotoCache` — flash-cached last photo

**Files:**
- Create: `src/photocache.h`
- Create: `src/photocache.cpp`

**Interfaces:**
- Consumes: `devLog` (Task 2), `renderJpeg(uint8_t*, size_t)` and `epaper` (`display.h`, already exist).
- Produces: `bool savePhotoCache(const uint8_t *buf, size_t len); bool hasCachedPhoto(); bool streamCachedPhoto(WebServer &server); bool renderCachedPhoto();` — Task 5 (net.cpp) uses `savePhotoCache`; Task 6 (portal.cpp) uses `streamCachedPhoto`; Task 7 (main.cpp) uses `renderCachedPhoto`.

- [ ] **Step 1: Write `src/photocache.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Flash-cached copy of the last successfully fetched/rendered photo --
// the original downloaded JPEG bytes, not a per-panel dithered
// framebuffer (see
// docs/superpowers/specs/2026-08-13-debug-visibility-design.md).
// Backed by LittleFS on the existing, previously-unused `spiffs`
// partition; mounted lazily (format-on-first-use) by whichever of these
// functions is called first.

// Overwrites the cached photo with buf/len. Returns false on a mount or
// write failure; never leaves a partially-written file behind.
bool savePhotoCache(const uint8_t *buf, size_t len);

// True if a cached photo file exists and is non-empty.
bool hasCachedPhoto();

// Streams the cached photo to an HTTP client as image/jpeg. Sends its own
// 404 and returns false if there's no cached photo.
bool streamCachedPhoto(WebServer &server);

// Reads the cached photo back into memory and renders it via
// display.h's renderJpeg() (fillScreen + dither into the sprite; caller
// still owns epaper.update()). Returns false if there's no cached photo
// or it fails to decode.
bool renderCachedPhoto();
```

- [ ] **Step 2: Write `src/photocache.cpp`**

```cpp
#include "photocache.h"
#include "devlog.h"
#include "display.h"
#include <LittleFS.h>

namespace {
constexpr const char *CACHE_PATH = "/last.jpg";
bool mounted = false;

bool ensureMounted() {
    if (mounted) return true;
    mounted = LittleFS.begin(true); // format on first mount
    if (!mounted) devLog.println("photocache: LittleFS mount failed");
    return mounted;
}
} // namespace

bool savePhotoCache(const uint8_t *buf, size_t len) {
    if (!ensureMounted()) return false;
    File f = LittleFS.open(CACHE_PATH, "w");
    if (!f) {
        devLog.println("photocache: open for write failed");
        return false;
    }
    size_t written = f.write(buf, len);
    f.close();
    if (written != len) {
        devLog.printf("photocache: short write (%u of %u)\n",
                      (unsigned)written, (unsigned)len);
        LittleFS.remove(CACHE_PATH); // don't leave a truncated cache behind
        return false;
    }
    return true;
}

bool hasCachedPhoto() {
    if (!ensureMounted()) return false;
    File f = LittleFS.open(CACHE_PATH, "r");
    if (!f) return false;
    bool nonEmpty = f.size() > 0;
    f.close();
    return nonEmpty;
}

bool streamCachedPhoto(WebServer &server) {
    if (!ensureMounted()) {
        server.send(404, "text/plain", "no cached photo");
        return false;
    }
    File f = LittleFS.open(CACHE_PATH, "r");
    if (!f || f.size() == 0) {
        if (f) f.close();
        server.send(404, "text/plain", "no cached photo");
        return false;
    }
    server.streamFile(f, "image/jpeg");
    f.close();
    return true;
}

bool renderCachedPhoto() {
    if (!ensureMounted()) return false;
    File f = LittleFS.open(CACHE_PATH, "r");
    if (!f || f.size() == 0) {
        if (f) f.close();
        return false;
    }
    size_t len = f.size();
    uint8_t *buf = (uint8_t *)ps_malloc(len);
    if (!buf) {
        f.close();
        devLog.println("photocache: PSRAM alloc failed");
        return false;
    }
    size_t readLen = f.read(buf, len);
    f.close();
    if (readLen != len) {
        free(buf);
        devLog.println("photocache: short read");
        return false;
    }
    epaper.fillScreen(TFT_WHITE);
    bool ok = renderJpeg(buf, len);
    free(buf);
    return ok;
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS. (No caller yet, so this only checks `photocache.cpp`/`.h` build cleanly against `LittleFS`/`WebServer`/`display.h`.)

- [ ] **Step 4: Commit**

```bash
git add src/photocache.h src/photocache.cpp
git commit -m "Add PhotoCache: flash-cache the last fetched JPEG on LittleFS"
```

---

### Task 5: Save every successful fetch into the cache

**Files:**
- Modify: `src/net.cpp:148-186` (`fetchImage()`)

**Interfaces:**
- Consumes: `savePhotoCache(const uint8_t*, size_t)` from Task 4 (`#include "photocache.h"`).

- [ ] **Step 1: Add the include**

In `src/net.cpp`, add near the other local includes at the top:

```cpp
#include "photocache.h"
```

- [ ] **Step 2: Call `savePhotoCache` after a successful render**

In `fetchImage()`, this existing block:

```cpp
    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(sink.buf, sink.len);
    if (!rendered) {
        err = "that URL is not a baseline JPEG";
        return false;
    }
    return true;
}
```

becomes:

```cpp
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
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/net.cpp
git commit -m "fetchImage: cache every successfully rendered photo to flash"
```

---

### Task 6: Portal routes — `/last.jpg`, `/log`, `/debug`

**Files:**
- Modify: `src/portal.cpp` (add includes, 3 handlers, 3 route registrations)
- Modify: `src/portal_html.h` (add `DEBUG_HTML` template)
- Modify: `README.md` (document the 3 new routes)

**Interfaces:**
- Consumes: `devLog.snapshot()` (Task 2), `streamCachedPhoto(WebServer&)` (Task 4).

- [ ] **Step 1: Add includes to `src/portal.cpp`**

Add alongside the existing local includes at the top of the file:

```cpp
#include "devlog.h"
#include "photocache.h"
```

- [ ] **Step 2: Add the `DEBUG_HTML` template to `src/portal_html.h`**

Append at the end of the file, after `PORTAL_DONE_HTML`:

```cpp

// Debug page: the currently-cached photo plus the rolling Serial log,
// readable over HTTP with no interactive serial-monitor TTY needed.
inline const char DEBUG_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Debug</title>
<style>body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
img{max-width:100%;border:1px solid #ccc;border-radius:6px}
pre{white-space:pre-wrap;word-break:break-word;background:#f4f4f4;border-radius:6px;padding:.8rem;font-size:.8rem}
h2{font-size:1rem;margin:1.4rem 0 .3rem}</style>
</head><body>
<h1>Debug</h1>
<h2>Displayed now</h2>
<img src="/last.jpg" alt="currently displayed photo">
<h2>Log</h2>
<pre>%LOG%</pre>
</body></html>)HTML";
```

- [ ] **Step 3: Add the three handlers to `src/portal.cpp`**

Add just above `bool startPortal() {`:

```cpp
static void handleLastJpg() { streamCachedPhoto(server); }

static void handleLog() {
    server.send(200, "text/plain", devLog.snapshot());
}

static void handleDebug() {
    String page = FPSTR(DEBUG_HTML);
    page.replace("%LOG%", htmlEscape(devLog.snapshot()));
    server.send(200, "text/html", page);
}
```

- [ ] **Step 4: Register the routes**

In `startPortal()`, this existing block:

```cpp
        server.on("/", HTTP_GET, handleRoot);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/action/newpic", HTTP_POST, handleNewPic);
        server.on("/action/forgetwifi", HTTP_POST, handleForgetWifi);
        server.onNotFound(
```

becomes:

```cpp
        server.on("/", HTTP_GET, handleRoot);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/action/newpic", HTTP_POST, handleNewPic);
        server.on("/action/forgetwifi", HTTP_POST, handleForgetWifi);
        server.on("/last.jpg", HTTP_GET, handleLastJpg);
        server.on("/log", HTTP_GET, handleLog);
        server.on("/debug", HTTP_GET, handleDebug);
        server.onNotFound(
```

- [ ] **Step 5: Document the routes in README.md**

In the "Dev mode" paragraph block (the one ending "...no KEY1 needed."), add a new paragraph directly after it:

```markdown

While the portal's up (dev mode, or the 10-minute KEY1 status window),
`http://<name>.local/debug` shows the currently-displayed photo plus a
rolling copy of the last ~8KB of Serial output — handy when
`pio device monitor` isn't available (e.g. no interactive TTY). Plain-text
log alone: `/log`. Same unauthenticated-on-your-LAN threat model as the
rest of the portal.
```

- [ ] **Step 6: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/portal.cpp src/portal_html.h README.md
git commit -m "Portal: add /last.jpg, /log, /debug routes"
```

---

### Task 7: `runStatusMode()` — redisplay cached photo instead of always refetching

**Files:**
- Modify: `src/main.cpp:73-94` (`runStatusMode()`)
- Modify: `src/main.cpp:177-179` (setup()'s KEY1 branch)
- Modify: `src/main.cpp:231-233` (loop()'s dev-mode info branch)

**Interfaces:**
- Consumes: `renderCachedPhoto()` from Task 4.

- [ ] **Step 1: Add the include**

In `src/main.cpp`, add alongside the other local includes:

```cpp
#include "photocache.h"
```

- [ ] **Step 2: Rewrite `runStatusMode()`**

Replace the whole function body:

```cpp
static bool runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    drawStatusScreen(vbatMv, deltaMv, haveDelta);
    devLog.println("updating panel (takes ~20-30 s)...");
    setLed(LedMode::Heartbeat);
    epaper.update();
    setLed(LedMode::Solid);
    devLog.println("done");
    if (!connectWifi()) return false; // provisioning fallback already drew
    if (!startPortal()) { doFetchCycle(true); return true; }
    PortalResult r = runPortal(10 * 60 * 1000UL);
    switch (r) {
        case PortalResult::KeyExit: devLog.println("portal: KEY1 exit"); break;
        case PortalResult::Timeout: devLog.println("portal: idle timeout"); break;
        case PortalResult::Saved: break;      // logged in the handler
        case PortalResult::ForgetWifi: break; // next connect reopens provisioning
    }
    // Settings (rotation, url, ...) may have changed: reapply orientation
    // before the panel is redrawn either way.
    applyOrientation();
    applyUtcOffset(prefs.getLong("tzOff", 0)); // manual TZ applies either way

    if (r == PortalResult::Saved || r == PortalResult::ForgetWifi) {
        doFetchCycle(true); // a real setting changed -- show its effect now
        return true;
    }
    // KeyExit / Timeout: nothing changed. Redisplay the cached photo
    // instead of burning a network fetch -- and, since the default image
    // source is randomized, instead of silently swapping the picture just
    // because someone glanced at the status screen.
    setLed(LedMode::Heartbeat);
    if (renderCachedPhoto()) {
        devLog.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        devLog.println("done");
        setLed(LedMode::Solid);
    } else {
        setLed(LedMode::Solid);
        doFetchCycle(true); // no cache yet (e.g. first boot) -- fall back
    }
    return true;
}
```

- [ ] **Step 3: Simplify the two call sites**

In `setup()`, this:

```cpp
    } else if (btnBits & (1ULL << BTN_INFO)) {
        if (runStatusMode(vbatMv, deltaMv, haveDelta)) doFetchCycle(true);
        else showError("Wi-Fi connection failed");
    } else {
```

becomes:

```cpp
    } else if (btnBits & (1ULL << BTN_INFO)) {
        if (!runStatusMode(vbatMv, deltaMv, haveDelta))
            showError("Wi-Fi connection failed");
    } else {
```

In `loop()`, this:

```cpp
        if (info) {
            if (runStatusMode(vbatMv, deltaMv, haveDelta)) doFetchCycle(true);
            else showError("Wi-Fi connection failed");
        } else {
```

becomes:

```cpp
        if (info) {
            if (!runStatusMode(vbatMv, deltaMv, haveDelta))
                showError("Wi-Fi connection failed");
        } else {
```

- [ ] **Step 4: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "runStatusMode: redisplay cached photo on KeyExit/Timeout, only refetch on Saved/ForgetWifi"
```

---

### Task 8: Full verification pass

**Files:** none (verification only)

- [ ] **Step 1: Run the native test suite**

Run: `pio test -e native`
Expected: PASS, including the new `test_ring_log` cases from Task 1 alongside all existing suites (`test_battery`, `test_layout_math`, `test_quiet_hours`, `test_url_template`, `test_validate`, `test_wifi_strength`).

- [ ] **Step 2: Build every firmware env**

```bash
pio run -e ee02
pio run -e ee03
pio run -e ee04
pio run -e ee05
```

Expected: all 4 SUCCESS.

- [ ] **Step 3: Flash to hardware and verify manually**

Flash `ee02` (or whichever env matches the attached board) over USB, keep the computer attached so dev mode's portal stays up, then from a browser or `curl`:

- `http://<name>.local/debug` shows the currently-displayed photo under "Displayed now" and recent boot/fetch log lines under "Log".
- `http://<name>.local/log` returns the same log as plain text.
- Press KEY1, wait for the status screen, press KEY1 again without changing anything in the portal: `/log` should show no new `GET <url>` line from a fetch, and the panel should return to the same photo it showed before KEY1 was pressed.
- Press KEY1, change and save a setting (e.g. refresh interval) in the portal: a new `GET <url>` line appears in `/log` and the panel shows a freshly fetched photo, same as before this change.

This step needs a human with the physical board — the assistant can watch `/log`/`/debug` over HTTP but can't see the physical panel.

- [ ] **Step 4: Commit if Step 3 uncovered fixes**

If manual verification required any code changes, commit them now with a description of what was fixed. If nothing needed fixing, no commit is needed for this task.
