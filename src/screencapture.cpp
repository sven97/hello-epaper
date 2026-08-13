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
            uint16_t color = truePixelColor(srcX, srcY);
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
