# Panel Capture (/current, /previous) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `/current` (live, on-demand BMP of whatever's on the panel right now) and `/previous` (the panel content immediately before the most recent redraw) to the debug portal, so KEY1/error/provisioning screens are visible over HTTP, not just fetched photos.

**Architecture:** Pure BMP-encoding/downscale math in `src/logic/bmp_thumbnail.h` (host-testable, no Arduino deps). `src/screencapture.h/.cpp` is the only code touching `EPaper::readPixel()` (which already normalizes every panel's mono/gray/color sprite storage back to RGB565) — it streams `/current` live with no storage, and keeps one PSRAM-backed snapshot for `/previous`, captured by a one-line `snapshotPrevious()` call inserted at the top of every screen-drawing function.

**Tech Stack:** Arduino/ESP32 (PlatformIO), `WebServer`'s `setContentLength()`/`sendContent()` for streaming, Unity for native host tests.

## Global Constraints

- `THUMBNAIL_MAX_LONG_SIDE = 400` — captures are downscaled thumbnails, never full resolution, and never upscaled if the panel is already smaller.
- BMP format only (24bpp uncompressed) — no JPEG/PNG encoding.
- `/previous` storage is RAM (PSRAM via `ps_malloc`) only — no LittleFS, no persistence across deep sleep.
- `/current` performs no storage at all — generated fresh from the live sprite on every request.
- A failed `ps_malloc` in `snapshotPrevious()` leaves any existing previous snapshot in place rather than clearing it.

---

### Task 1: `bmp_thumbnail.h` pure logic + native tests

**Files:**
- Create: `src/logic/bmp_thumbnail.h`
- Test: `test/test_bmp_thumbnail/main.cpp`

**Interfaces:**
- Produces: `constexpr int THUMBNAIL_MAX_LONG_SIDE = 400;` `constexpr size_t BMP_HEADER_SIZE = 54;` `void computeThumbnailSize(int srcW, int srcH, int maxLongSide, int &outW, int &outH);` `int nearestSourceCoord(int destIdx, int destSize, int srcSize);` `void rgb565ToRgb888(uint16_t color, uint8_t &r, uint8_t &g, uint8_t &b);` `int bmpRowStride(int width);` `size_t bmpFileSize(int width, int height);` `void writeBmpHeader(uint8_t *out, int width, int height);` `int bmpSourceRowForFileRow(int fileRow, int height);` — Task 2 (`screencapture.cpp`) uses all of these.

- [ ] **Step 1: Write the failing test**

Create `test/test_bmp_thumbnail/main.cpp`:

```cpp
#include <unity.h>
#include "logic/bmp_thumbnail.h"

void setUp() {}
void tearDown() {}

void test_no_upscale_when_within_cap() {
    int w, h;
    computeThumbnailSize(200, 100, 400, w, h);
    TEST_ASSERT_EQUAL_INT(200, w);
    TEST_ASSERT_EQUAL_INT(100, h);
}

void test_downscale_wide() {
    int w, h;
    computeThumbnailSize(1600, 1200, 400, w, h);
    TEST_ASSERT_EQUAL_INT(400, w);
    TEST_ASSERT_EQUAL_INT(300, h);
}

void test_downscale_tall() {
    int w, h; // EE02: 1200x1600 portrait
    computeThumbnailSize(1200, 1600, 400, w, h);
    TEST_ASSERT_EQUAL_INT(300, w);
    TEST_ASSERT_EQUAL_INT(400, h);
}

void test_nearest_source_coord_basic() {
    TEST_ASSERT_EQUAL_INT(0, nearestSourceCoord(0, 400, 1600));
    TEST_ASSERT_EQUAL_INT(800, nearestSourceCoord(200, 400, 1600));
    TEST_ASSERT_EQUAL_INT(1596, nearestSourceCoord(399, 400, 1600));
}

void test_nearest_source_coord_single_dest_pixel() {
    TEST_ASSERT_EQUAL_INT(0, nearestSourceCoord(0, 1, 1600));
}

void test_rgb565_to_rgb888_white() {
    uint8_t r, g, b;
    rgb565ToRgb888(0xFFFF, r, g, b);
    TEST_ASSERT_EQUAL_UINT8(255, r);
    TEST_ASSERT_EQUAL_UINT8(255, g);
    TEST_ASSERT_EQUAL_UINT8(255, b);
}

void test_rgb565_to_rgb888_black() {
    uint8_t r, g, b;
    rgb565ToRgb888(0x0000, r, g, b);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_UINT8(0, g);
    TEST_ASSERT_EQUAL_UINT8(0, b);
}

void test_rgb565_to_rgb888_pure_red() {
    uint8_t r, g, b;
    rgb565ToRgb888(0xF800, r, g, b); // 11111 000000 00000
    TEST_ASSERT_EQUAL_UINT8(255, r);
    TEST_ASSERT_EQUAL_UINT8(0, g);
    TEST_ASSERT_EQUAL_UINT8(0, b);
}

void test_bmp_row_stride_no_padding_needed() {
    TEST_ASSERT_EQUAL_INT(12, bmpRowStride(4)); // 4*3=12, already /4
}

void test_bmp_row_stride_with_padding() {
    TEST_ASSERT_EQUAL_INT(16, bmpRowStride(5)); // 5*3=15 -> padded to 16
}

void test_bmp_file_size() {
    TEST_ASSERT_EQUAL_UINT32(78, (uint32_t)bmpFileSize(4, 2)); // 54 + 12*2
}

void test_bmp_header_bytes_exact() {
    uint8_t out[BMP_HEADER_SIZE];
    writeBmpHeader(out, 2, 1); // rowStride(2)=8 (padded), fileSize=54+8=62
    static const uint8_t expected[BMP_HEADER_SIZE] = {
        'B', 'M',                   // bfType
        0x3E, 0x00, 0x00, 0x00,     // bfSize = 62
        0x00, 0x00,                 // reserved1
        0x00, 0x00,                 // reserved2
        0x36, 0x00, 0x00, 0x00,     // bfOffBits = 54
        0x28, 0x00, 0x00, 0x00,     // biSize = 40
        0x02, 0x00, 0x00, 0x00,     // biWidth = 2
        0x01, 0x00, 0x00, 0x00,     // biHeight = 1
        0x01, 0x00,                 // biPlanes = 1
        0x18, 0x00,                 // biBitCount = 24
        0x00, 0x00, 0x00, 0x00,     // biCompression = 0
        0x08, 0x00, 0x00, 0x00,     // biSizeImage = 8
        0x00, 0x00, 0x00, 0x00,     // biXPelsPerMeter
        0x00, 0x00, 0x00, 0x00,     // biYPelsPerMeter
        0x00, 0x00, 0x00, 0x00,     // biClrUsed
        0x00, 0x00, 0x00, 0x00,     // biClrImportant
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, BMP_HEADER_SIZE);
}

void test_bmp_source_row_for_file_row() {
    TEST_ASSERT_EQUAL_INT(3, bmpSourceRowForFileRow(0, 4));
    TEST_ASSERT_EQUAL_INT(2, bmpSourceRowForFileRow(1, 4));
    TEST_ASSERT_EQUAL_INT(1, bmpSourceRowForFileRow(2, 4));
    TEST_ASSERT_EQUAL_INT(0, bmpSourceRowForFileRow(3, 4));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_upscale_when_within_cap);
    RUN_TEST(test_downscale_wide);
    RUN_TEST(test_downscale_tall);
    RUN_TEST(test_nearest_source_coord_basic);
    RUN_TEST(test_nearest_source_coord_single_dest_pixel);
    RUN_TEST(test_rgb565_to_rgb888_white);
    RUN_TEST(test_rgb565_to_rgb888_black);
    RUN_TEST(test_rgb565_to_rgb888_pure_red);
    RUN_TEST(test_bmp_row_stride_no_padding_needed);
    RUN_TEST(test_bmp_row_stride_with_padding);
    RUN_TEST(test_bmp_file_size);
    RUN_TEST(test_bmp_header_bytes_exact);
    RUN_TEST(test_bmp_source_row_for_file_row);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_bmp_thumbnail`
Expected: FAIL to compile — `logic/bmp_thumbnail.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `src/logic/bmp_thumbnail.h`:

```cpp
#pragma once
// BMP encoding + nearest-neighbor downscale math for panel screen
// captures. Pure logic: host-testable, no Arduino deps -- same pattern
// as ring_log.h.
#include <cstddef>
#include <cstdint>

constexpr int THUMBNAIL_MAX_LONG_SIDE = 400;
constexpr size_t BMP_HEADER_SIZE = 54;

// Nearest-neighbor downscale, capping the long side at maxLongSide while
// preserving aspect ratio. Never upscales.
inline void computeThumbnailSize(int srcW, int srcH, int maxLongSide,
                                 int &outW, int &outH) {
    int longSide = srcW > srcH ? srcW : srcH;
    if (longSide <= maxLongSide) {
        outW = srcW;
        outH = srcH;
        return;
    }
    if (srcW >= srcH) {
        outW = maxLongSide;
        outH = (int)((long long)srcH * maxLongSide / srcW);
        if (outH < 1) outH = 1;
    } else {
        outH = maxLongSide;
        outW = (int)((long long)srcW * maxLongSide / srcH);
        if (outW < 1) outW = 1;
    }
}

// Maps a destination pixel coordinate to the nearest source coordinate.
inline int nearestSourceCoord(int destIdx, int destSize, int srcSize) {
    if (destSize <= 1) return 0;
    long long v = (long long)destIdx * srcSize / destSize;
    if (v >= srcSize) v = srcSize - 1;
    return (int)v;
}

// RGB565 -> individual 8-bit channels (5/6/5 bit widths expanded to 8).
inline void rgb565ToRgb888(uint16_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
    uint8_t r5 = (uint8_t)((color >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((color >> 5) & 0x3F);
    uint8_t b5 = (uint8_t)(color & 0x1F);
    r = (uint8_t)((r5 * 255 + 15) / 31);
    g = (uint8_t)((g6 * 255 + 31) / 63);
    b = (uint8_t)((b5 * 255 + 15) / 31);
}

// Per-row byte count for a 24bpp BMP, padded up to a multiple of 4.
inline int bmpRowStride(int width) {
    int bytes = width * 3;
    return (bytes + 3) & ~3;
}

// Total file size: header + bmpRowStride(width) * height.
inline size_t bmpFileSize(int width, int height) {
    return BMP_HEADER_SIZE + (size_t)bmpRowStride(width) * (size_t)height;
}

namespace bmp_detail {
inline void putU16(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
}
inline void putU32(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}
} // namespace bmp_detail

// Writes the 54-byte BITMAPFILEHEADER + BITMAPINFOHEADER for an
// uncompressed 24bpp bitmap of the given dimensions into out[0..53].
inline void writeBmpHeader(uint8_t *out, int width, int height) {
    using namespace bmp_detail;
    size_t fileSize = bmpFileSize(width, height);
    out[0] = 'B';
    out[1] = 'M';
    putU32(out + 2, (uint32_t)fileSize);
    putU16(out + 6, 0);
    putU16(out + 8, 0);
    putU32(out + 10, (uint32_t)BMP_HEADER_SIZE);
    putU32(out + 14, 40);
    putU32(out + 18, (uint32_t)width);
    putU32(out + 22, (uint32_t)height); // positive = bottom-up
    putU16(out + 26, 1);
    putU16(out + 28, 24);
    putU32(out + 30, 0);
    putU32(out + 34, (uint32_t)(bmpRowStride(width) * height));
    putU32(out + 38, 0);
    putU32(out + 42, 0);
    putU32(out + 46, 0);
    putU32(out + 50, 0);
}

// BMP stores rows bottom-up: the fileRow'th row written to the stream is
// image row bmpSourceRowForFileRow(fileRow, height), not fileRow itself.
inline int bmpSourceRowForFileRow(int fileRow, int height) {
    return height - 1 - fileRow;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_bmp_thumbnail`
Expected: PASS, all 13 assertions green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/bmp_thumbnail.h test/test_bmp_thumbnail/main.cpp
git commit -m "Add bmp_thumbnail: pure BMP encoding + downscale math"
```

---

### Task 2: `screencapture.h/.cpp` Arduino glue

**Files:**
- Create: `src/screencapture.h`
- Create: `src/screencapture.cpp`

**Interfaces:**
- Consumes: `bmp_thumbnail.h` (Task 1); `epaper` and `EPaper::readPixel()`/`width()`/`height()` (`display.h`, already exist).
- Produces: `bool streamCurrentBmp(WebServer &server); void snapshotPrevious(); bool streamPreviousBmp(WebServer &server);` — Task 3 calls `snapshotPrevious()`; Task 4 (portal routes) calls all three.

- [ ] **Step 1: Write `src/screencapture.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Live panel-capture debug routes (see
// docs/superpowers/specs/2026-08-13-panel-capture-design.md). Both read
// epaper's sprite via readPixel(), which normalizes every supported
// panel's storage format (mono/gray/color) back to RGB565 -- no
// per-panel branching needed here.

// GET /current: no storage. Streams a live downscaled BMP of whatever's
// currently in the sprite. Sends its own 500 and returns false on a
// degenerate 0x0 panel size.
bool streamCurrentBmp(WebServer &server);

// Captures the sprite's CURRENT content (before it's about to be
// overwritten) into a PSRAM buffer, replacing whatever was captured
// before. Call at the top of every function that's about to draw a new
// screen, before any drawing happens. A failed capture (e.g. PSRAM
// exhausted) silently leaves the previous snapshot in place.
void snapshotPrevious();

// GET /previous: streams the stored snapshot as image/bmp. Sends its own
// 404 and returns false if snapshotPrevious() has never been called.
bool streamPreviousBmp(WebServer &server);
```

- [ ] **Step 2: Write `src/screencapture.cpp`**

```cpp
#include "screencapture.h"
#include "display.h"
#include "logic/bmp_thumbnail.h"
#include <cstring>

namespace {
uint8_t *previousBuf = nullptr;
size_t previousLen = 0;

// Encodes a thumbnail-sized BMP of the sprite's current content. When
// server is non-null, streams it directly (outBuf/outLen unused). When
// server is null, writes a freshly ps_malloc'd buffer to *outBuf/*outLen
// instead. Single implementation shared by streamCurrentBmp() (streaming)
// and snapshotPrevious() (buffered) so the row-sampling logic exists in
// exactly one place.
bool encodeSpriteBmp(WebServer *server, uint8_t **outBuf, size_t *outLen) {
    int srcW = epaper.width(), srcH = epaper.height();
    if (srcW <= 0 || srcH <= 0) return false;
    int w, h;
    computeThumbnailSize(srcW, srcH, THUMBNAIL_MAX_LONG_SIDE, w, h);
    int stride = bmpRowStride(w);
    size_t total = bmpFileSize(w, h);

    uint8_t header[BMP_HEADER_SIZE];
    writeBmpHeader(header, w, h);

    uint8_t *rowBuf = (uint8_t *)malloc(stride);
    if (!rowBuf) return false;

    uint8_t *buf = nullptr;
    if (server) {
        server->setContentLength(total);
        server->send(200, "image/bmp", "");
        server->sendContent((const char *)header, BMP_HEADER_SIZE);
    } else {
        buf = (uint8_t *)ps_malloc(total);
        if (!buf) {
            free(rowBuf);
            return false;
        }
        memcpy(buf, header, BMP_HEADER_SIZE);
    }

    for (int fileRow = 0; fileRow < h; fileRow++) {
        int destRow = bmpSourceRowForFileRow(fileRow, h);
        int srcY = nearestSourceCoord(destRow, h, srcH);
        memset(rowBuf, 0, stride);
        for (int x = 0; x < w; x++) {
            int srcX = nearestSourceCoord(x, w, srcW);
            uint16_t color = epaper.readPixel(srcX, srcY);
            uint8_t r, g, b;
            rgb565ToRgb888(color, r, g, b);
            rowBuf[x * 3 + 0] = b; // BMP pixel order is BGR
            rowBuf[x * 3 + 1] = g;
            rowBuf[x * 3 + 2] = r;
        }
        if (server) {
            server->sendContent((const char *)rowBuf, stride);
        } else {
            memcpy(buf + BMP_HEADER_SIZE + (size_t)fileRow * stride, rowBuf, stride);
        }
    }
    free(rowBuf);
    if (!server) {
        *outBuf = buf;
        *outLen = total;
    }
    return true;
}
} // namespace

bool streamCurrentBmp(WebServer &server) {
    if (!encodeSpriteBmp(&server, nullptr, nullptr)) {
        server.send(500, "text/plain", "capture failed");
        return false;
    }
    return true;
}

void snapshotPrevious() {
    uint8_t *buf = nullptr;
    size_t len = 0;
    if (!encodeSpriteBmp(nullptr, &buf, &len)) return; // leave old snapshot in place
    if (previousBuf) free(previousBuf);
    previousBuf = buf;
    previousLen = len;
}

bool streamPreviousBmp(WebServer &server) {
    if (!previousBuf) {
        server.send(404, "text/plain", "no previous screen captured yet");
        return false;
    }
    server.setContentLength(previousLen);
    server.send(200, "image/bmp", "");
    server.sendContent((const char *)previousBuf, previousLen);
    return true;
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS. (Nothing calls these functions yet, so this only checks `screencapture.cpp`/`.h` build cleanly.)

- [ ] **Step 4: Commit**

```bash
git add src/screencapture.h src/screencapture.cpp
git commit -m "Add screencapture: live /current streaming + /previous PSRAM snapshot"
```

---

### Task 3: Wire `snapshotPrevious()` into the 5 screen-drawing functions

**Files:**
- Modify: `src/ui.cpp` (`drawStatusScreen()`)
- Modify: `src/net.cpp` (`showProvisioningScreen()`, `fetchImage()`)
- Modify: `src/photocache.cpp` (`renderCachedPhoto()`)
- Modify: `src/display.cpp` (`showError()`)

**Interfaces:**
- Consumes: `snapshotPrevious()` from Task 2 (`#include "screencapture.h"`).

- [ ] **Step 1: Add the include to all 4 files**

All 4 already have `#include "display.h"` as an exact, standalone line (same trick used for `devlog.h` earlier):

```bash
cd /Users/sven/Developer/hello-epaper
sed -i '' 's/^#include "display.h"$/&\n#include "screencapture.h"/' \
  src/ui.cpp src/net.cpp src/photocache.cpp src/display.cpp
grep -c '#include "screencapture.h"' src/ui.cpp src/net.cpp src/photocache.cpp src/display.cpp
```

Expected: each prints `1`.

- [ ] **Step 2: `ui.cpp` — `drawStatusScreen()`**

This existing block:

```cpp
    const String url = portalUrl();
    const String lastIp = prefs.getString("lastIp", "");

    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
```

becomes:

```cpp
    const String url = portalUrl();
    const String lastIp = prefs.getString("lastIp", "");

    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
```

- [ ] **Step 3: `net.cpp` — `showProvisioningScreen()`**

Note: the function to change is `showProvisioningScreen()` (the one that actually draws), not `showProvisioningScreenOnce()` (a once-guard wrapper around it that never touches the sprite itself). This existing block:

```cpp
static void showProvisioningScreen() {
    const LayoutMetrics lm = currentLayout();
    const int cx = epaper.width() / 2;
    epaper.fillScreen(TFT_WHITE);
```

becomes:

```cpp
static void showProvisioningScreen() {
    const LayoutMetrics lm = currentLayout();
    const int cx = epaper.width() / 2;
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
```

- [ ] **Step 4: `net.cpp` — `fetchImage()`**

This existing block:

```cpp
    if (sink.len == 0) {
        err = "image server sent no data";
        return false;
    }
    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(sink.buf, sink.len);
```

becomes:

```cpp
    if (sink.len == 0) {
        err = "image server sent no data";
        return false;
    }
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    bool rendered = renderJpeg(sink.buf, sink.len);
```

- [ ] **Step 5: `photocache.cpp` — `renderCachedPhoto()`**

This existing block:

```cpp
    if (readLen != len) {
        free(buf);
        devLog.println("photocache: short read");
        return false;
    }
    epaper.fillScreen(TFT_WHITE);
    bool ok = renderJpeg(buf, len);
```

becomes:

```cpp
    if (readLen != len) {
        free(buf);
        devLog.println("photocache: short read");
        return false;
    }
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    bool ok = renderJpeg(buf, len);
```

- [ ] **Step 6: `display.cpp` — `showError()`**

This existing block:

```cpp
void showError(const String &msg) {
    const int cx = epaper.width() / 2, cy = epaper.height() / 2;
    epaper.fillScreen(TFT_WHITE);
```

becomes:

```cpp
void showError(const String &msg) {
    const int cx = epaper.width() / 2, cy = epaper.height() / 2;
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
```

- [ ] **Step 7: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/ui.cpp src/net.cpp src/photocache.cpp src/display.cpp
git commit -m "Capture the outgoing screen before every redraw (snapshotPrevious)"
```

---

### Task 4: Portal routes + `/debug` page update

**Files:**
- Modify: `src/portal.cpp` (include, 2 handlers, 2 route registrations)
- Modify: `src/portal_html.h` (`DEBUG_HTML` gets two more images)

**Interfaces:**
- Consumes: `streamCurrentBmp(WebServer&)`, `streamPreviousBmp(WebServer&)` (Task 2).

- [ ] **Step 1: Add the include to `src/portal.cpp`**

Add alongside the other local includes (e.g. right after `#include "photocache.h"`):

```cpp
#include "screencapture.h"
```

- [ ] **Step 2: Add the two handlers**

In `src/portal.cpp`, this existing block:

```cpp
static void handleLastJpg() { streamCachedPhoto(server); }

static void handleLog() {
```

becomes:

```cpp
static void handleLastJpg() { streamCachedPhoto(server); }
static void handleCurrent() { streamCurrentBmp(server); }
static void handlePrevious() { streamPreviousBmp(server); }

static void handleLog() {
```

- [ ] **Step 3: Register the routes**

This existing block:

```cpp
        server.on("/last.jpg", HTTP_GET, handleLastJpg);
        server.on("/log", HTTP_GET, handleLog);
        server.on("/debug", HTTP_GET, handleDebug);
        server.onNotFound(
```

becomes:

```cpp
        server.on("/last.jpg", HTTP_GET, handleLastJpg);
        server.on("/current", HTTP_GET, handleCurrent);
        server.on("/previous", HTTP_GET, handlePrevious);
        server.on("/log", HTTP_GET, handleLog);
        server.on("/debug", HTTP_GET, handleDebug);
        server.onNotFound(
```

- [ ] **Step 4: Update `DEBUG_HTML` in `src/portal_html.h`**

This existing block:

```cpp
<h1>Debug</h1>
<h2>Displayed now</h2>
<img src="/last.jpg" alt="currently displayed photo">
<h2>Log</h2>
<pre>%LOG%</pre>
</body></html>)HTML";
```

becomes:

```cpp
<h1>Debug</h1>
<h2>Displayed now (last fetched photo)</h2>
<img src="/last.jpg" alt="last fetched photo">
<h2>On the panel right now</h2>
<img src="/current" alt="live panel capture">
<h2>On the panel just before that</h2>
<img src="/previous" alt="previous panel capture">
<h2>Log</h2>
<pre>%LOG%</pre>
</body></html>)HTML";
```

- [ ] **Step 5: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/portal.cpp src/portal_html.h
git commit -m "Portal: add /current, /previous routes and wire them into /debug"
```

---

### Task 5: Full verification pass

**Files:** none (verification only)

- [ ] **Step 1: Run the native test suite**

Run: `pio test -e native`
Expected: PASS, including the new `test_bmp_thumbnail` cases from Task 1 alongside every existing suite (`test_ring_log`, `test_quiet_hours`, `test_layout_math`, `test_validate`, `test_url_template`, `test_wifi_strength`, `test_battery`).

- [ ] **Step 2: Build every firmware env**

```bash
pio run -e ee02
pio run -e ee03
pio run -e ee04
pio run -e ee05
```

Expected: all 4 SUCCESS.

- [ ] **Step 3: Flash to hardware and verify manually**

Flash to the attached board, keep the computer attached so dev mode's portal stays up, then from a browser or `curl`:

- `curl -o current.bmp http://<name>.local/current` and open it — should visually match whatever's currently on the physical panel.
- Press KEY1. Before pressing it again, `curl -o previous.bmp http://<name>.local/previous` — should show the photo that was on the panel *before* KEY1 was pressed (not the status screen).
- `curl -o current2.bmp http://<name>.local/current` while the status screen is showing — should show the status screen itself.
- Open `http://<name>.local/debug` in a browser and confirm all three images (`/last.jpg`, `/current`, `/previous`) render correctly alongside the log.

This step needs a human with the physical board — the assistant can fetch and inspect the BMPs over HTTP but can't independently confirm they match the physical panel's appearance without those images being shared back.

- [ ] **Step 4: Commit if Step 3 uncovered fixes**

If manual verification required any code changes, commit them now with a description of what was fixed. If nothing needed fixing, no commit is needed for this task.
