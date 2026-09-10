#include "display.h"
#include "screencapture.h"
#include "photocache.h"
#include "devlog.h"
#include "config.h"
#include "settings.h"
#include "state.h"
#include "ui.h"
#include <JPEGDecoder.h>
#include <qrcode.h>

EPaper epaper;

void applyOrientation() { epaper.setRotation(settings.rotation); }

// The EE02 panel is portrait-native (1200x1600) -- rotation 0 is already
// portrait -- so this guard has nothing to swap. Kept as a type so the
// status/onboarding/error call sites don't churn.
PortraitScope::PortraitScope() {}
PortraitScope::~PortraitScope() {}

void drawStatusIcon(const uint8_t *bitmap, int x, int y, uint32_t fgColor) {
    epaper.drawBitmap(x, y, bitmap, ICON_W, ICON_H, fgColor);
}

void drawKeycap(const char *digit, int cx, int cy, int sizePx, uint32_t fgColor) {
    const int half = sizePx / 2;
    epaper.drawRoundRect(cx - half, cy - half, sizePx, sizePx, sizePx / 6, fgColor);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextColor(fgColor, TFT_WHITE);
    epaper.setTextSize(1);
    epaper.drawString(digit, cx, cy, 2); // classic Font2, legible at any legend size
}

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

// Panel colour index (drawPixel stores it directly) + sRGB approximation
// used as the dithering target. idx is uint32_t to match the library's
// TFT_* colour constants. The EE02 panel is Spectra-6: black, white, and
// four saturated primaries.
struct PaletteEntry { uint32_t idx; int16_t r, g, b; };

static const PaletteEntry PALETTE[] = {
    {TFT_WHITE,  255, 255, 255},
    {TFT_BLACK,  0,   0,   0  },
    {TFT_RED,    255, 0,   0  },
    {TFT_YELLOW, 255, 255, 0  },
    {TFT_GREEN,  0,   255, 0  },
    {TFT_BLUE,   0,   0,   255},
};
static const int PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);

// See the declaration in display.h for why EPaper::readPixel() can't be
// used for this instead.
uint16_t truePixelColor(int x, int y) {
    uint32_t raw = (uint32_t)epaper.readPixelValue(x, y);
    int depth = epaper.getColorDepth();
    if (depth == 16) return (uint16_t)raw; // already a true RGB565 value
    // 1bpp: drawPixel() only tests idx truthiness (see ditherToPanel's
    // mono fallback comment), not a bitmask -- match truthiness, not
    // bits. 4/8bpp: drawPixel() masks the low nibble/byte directly --
    // match on that mask instead.
    uint32_t mask = (depth == 1) ? 0 : ((1u << depth) - 1);
    for (int i = 0; i < PALETTE_SIZE; i++) {
        uint32_t idx = PALETTE[i].idx;
        bool match = mask ? ((idx & mask) == (raw & mask))
                          : ((idx != 0) == (raw != 0));
        if (match) {
            uint8_t r = (uint8_t)PALETTE[i].r;
            uint8_t g = (uint8_t)PALETTE[i].g;
            uint8_t b = (uint8_t)PALETTE[i].b;
            return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    return 0; // no match -- fall back to black rather than garbage
}

// Floyd-Steinberg dither the RGB565 frame down to the panel's palette.
// Raw RGB565 must never be pushed at 4 bpp: the sprite stores color &
// 0x0F there, i.e. it expects palette nibbles, not RGB values.
static bool ditherToPanel(const uint16_t *fb, int w, int h) {
    devLog.println("dithering to display palette...");
    const int stride = (w + 2) * 3; // per-channel error, 1-px guard each side
    int16_t *errs = (int16_t *)calloc(2 * stride, sizeof(int16_t));
    if (!errs) {
        devLog.println("dither buffer alloc failed");
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
            for (int i = 0; i < PALETTE_SIZE; i++) {
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
    devLog.println("dithering done");
    return true;
}

// Decode the JPEG into a full RGB565 frame in PSRAM, then dither it to
// the panel. Returns false if the JPEG doesn't decode to sane dimensions.
bool renderJpeg(uint8_t *buf, size_t len) {
    JpegDec.decodeArray(buf, len);
    const int w = JpegDec.width, h = JpegDec.height;
    devLog.printf("jpeg: %d x %d, MCU %d x %d\n", w, h,
                  JpegDec.MCUWidth, JpegDec.MCUHeight);
    if (w <= 0 || h <= 0 || w > epaper.width() || h > epaper.height()) {
        JpegDec.abort();
        devLog.println("bad jpeg dimensions");
        return false;
    }
    uint16_t *fb = (uint16_t *)ps_malloc((size_t)w * h * sizeof(uint16_t));
    if (!fb) {
        JpegDec.abort();
        devLog.println("PSRAM alloc for frame failed");
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

// Full-panel error screen. Drawn when someone is watching (button-
// initiated actions), and — once per outage — from an unattended wake
// once failures have persisted long enough to escalate (see main.cpp's
// maybeShowStuckError()). An ordinary unattended failure still keeps the
// photo untouched; this is the deliberate exception.
void showError(const String &msg, ErrorKind kind) {
    ScreenContent content = gatherLiveContent(lastVbatMv, 0, false);
    content.errorKind = kind;
    strncpy(content.errorMsg, msg.c_str(), sizeof(content.errorMsg) - 1);
    // A Wi-Fi failure blanks the signal row; an image failure leaves the
    // real SSID/signal that gatherLiveContent() read from the last good
    // connection -- the network isn't the problem.
    if (kind == ErrorKind::Wifi)
        strncpy(content.wifiBase, "failed", sizeof(content.wifiBase) - 1);
    snapshotPrevious();
    PortraitScope portrait;
    // Error card over the last good image (white only if there's no cache).
    if (!renderCachedPhoto()) epaper.fillScreen(TFT_WHITE);
    drawFrameScreen(ScreenState::Error, content);
    epaper.update();
}
