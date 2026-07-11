#include "display.h"
#include "config.h"
#include "settings.h"
#include "state.h"
#include <JPEGDecoder.h>

EPaper epaper;

void applyOrientation() { epaper.setRotation(settings.rotation); }

// Spectra 6 palette: panel nibble index (drawPixel stores it directly at
// 4 bpp) + sRGB approximation used as the dithering target.
struct PaletteEntry { uint8_t idx; int16_t r, g, b; };
static const PaletteEntry PALETTE[6] = {
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
static bool ditherToPanel(const uint16_t *fb, int w, int h) {
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

void showError(const String &msg) {
    epaper.fillScreen(TFT_WHITE);
    epaper.setTextColor(TFT_RED, TFT_WHITE);
    epaper.drawString("ERROR", 20, 40, 4);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    epaper.drawString(msg, 20, 120, 4);
    epaper.update();
}
