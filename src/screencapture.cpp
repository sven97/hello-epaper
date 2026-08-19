#include "screencapture.h"
#include "display.h"
#include "logic/bmp_thumbnail.h"
#include <climits>
#include <cstring>

namespace {
uint8_t *previousBuf = nullptr;
size_t previousLen = 0;

// Encodes a BMP of the sprite's current content, downscaled so its long
// side is at most maxLongSide (pass INT_MAX for full resolution -- never
// upscales regardless). When server is non-null, streams it directly
// (outBuf/outLen unused). When server is null, writes a freshly
// ps_malloc'd buffer to *outBuf/*outLen instead. Single implementation
// shared by streamCurrentBmp() (streaming, full resolution) and
// snapshotPrevious() (buffered, thumbnail-capped -- see their callers for
// why the two differ) so the row-sampling logic exists in exactly one
// place.
bool encodeSpriteBmp(WebServer *server, uint8_t **outBuf, size_t *outLen,
                     int maxLongSide) {
    int srcW = epaper.width(), srcH = epaper.height();
    if (srcW <= 0 || srcH <= 0) return false;
    int w, h;
    computeThumbnailSize(srcW, srcH, maxLongSide, w, h);
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
        int syStart, syEnd;
        sourceRangeForDest(destRow, h, srcH, syStart, syEnd);
        memset(rowBuf, 0, stride);
        for (int x = 0; x < w; x++) {
            int sxStart, sxEnd;
            sourceRangeForDest(x, w, srcW, sxStart, sxEnd);
            // Box-filter average over every source pixel this destination
            // pixel covers -- NOT a single nearest-neighbor sample. Panel
            // content is Floyd-Steinberg dithered, which scatters
            // per-pixel color noise to fake continuous tone; point-
            // sampling that noise at a downscaled stride is close to
            // random (looks like static), while averaging the box
            // recovers the intended color, the same way mipmap
            // downscaling handles high-frequency source content.
            uint32_t rSum = 0, gSum = 0, bSum = 0;
            int count = 0;
            for (int srcY = syStart; srcY < syEnd; srcY++) {
                for (int srcX = sxStart; srcX < sxEnd; srcX++) {
                    uint16_t color = truePixelColor(srcX, srcY);
                    uint8_t r, g, b;
                    rgb565ToRgb888(color, r, g, b);
                    rSum += r;
                    gSum += g;
                    bSum += b;
                    count++;
                }
            }
            rowBuf[x * 3 + 0] = (uint8_t)(bSum / count); // BMP pixel order is BGR
            rowBuf[x * 3 + 1] = (uint8_t)(gSum / count);
            rowBuf[x * 3 + 2] = (uint8_t)(rSum / count);
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
    // Full resolution: streaming needs no full-image buffer (just one
    // row-sized scratch buffer), so there's no memory-pressure reason to
    // downscale here the way snapshotPrevious() must.
    if (!encodeSpriteBmp(&server, nullptr, nullptr, INT_MAX)) {
        server.send(500, "text/plain", "capture failed");
        return false;
    }
    return true;
}

void snapshotPrevious() {
    // Thumbnail-capped: this buffer persists in PSRAM until the next
    // screen change, alongside other large PSRAM users (e.g. the JPEG
    // decode buffer during a fetch) -- full resolution here risks PSRAM
    // exhaustion, unlike streamCurrentBmp()'s zero-buffer streaming.
    uint8_t *buf = nullptr;
    size_t len = 0;
    if (!encodeSpriteBmp(nullptr, &buf, &len, THUMBNAIL_MAX_LONG_SIDE))
        return; // leave old snapshot in place
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
