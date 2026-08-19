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

// Maps a destination pixel coordinate to the half-open [start, end) range
// of source coordinates it covers, partitioning the full source range
// contiguously and without overlap across all destIdx in [0, destSize).
// Used for box-filter downscale averaging (unlike nearestSourceCoord's
// single-sample point mapping): reading only every Nth source pixel
// aliases badly against dithered panel content, since Floyd-Steinberg
// dithering scatters per-pixel color noise that a point sample can't
// reconstruct -- averaging the whole box recovers the intended color.
// When destSize == srcSize (no downscale), every range is exactly one
// pixel wide, matching nearestSourceCoord's identity mapping exactly.
inline void sourceRangeForDest(int destIdx, int destSize, int srcSize,
                                int &start, int &end) {
    if (destSize <= 1) {
        start = 0;
        end = srcSize;
        return;
    }
    long long s = (long long)destIdx * srcSize / destSize;
    long long e = (long long)(destIdx + 1) * srcSize / destSize;
    if (e <= s) e = s + 1;
    if (e > srcSize) e = srcSize;
    start = (int)s;
    end = (int)e;
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
