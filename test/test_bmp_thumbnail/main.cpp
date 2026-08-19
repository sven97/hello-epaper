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

void test_source_range_for_dest_downscale() {
    int start, end;
    sourceRangeForDest(0, 400, 1600, start, end);
    TEST_ASSERT_EQUAL_INT(0, start);
    TEST_ASSERT_EQUAL_INT(4, end);
    sourceRangeForDest(200, 400, 1600, start, end);
    TEST_ASSERT_EQUAL_INT(800, start);
    TEST_ASSERT_EQUAL_INT(804, end);
    sourceRangeForDest(399, 400, 1600, start, end);
    TEST_ASSERT_EQUAL_INT(1596, start);
    TEST_ASSERT_EQUAL_INT(1600, end); // clamped to srcSize, not 1604
}

void test_source_range_for_dest_no_downscale_is_single_pixel() {
    // destSize == srcSize (no scaling): every destination pixel must map
    // to exactly one source pixel, matching nearestSourceCoord's identity
    // mapping -- averaging over more than one pixel here would blur a
    // full-resolution capture that doesn't need it.
    for (int i = 0; i < 1600; i++) {
        int start, end;
        sourceRangeForDest(i, 1600, 1600, start, end);
        TEST_ASSERT_EQUAL_INT(i, start);
        TEST_ASSERT_EQUAL_INT(i + 1, end);
    }
}

void test_source_range_for_dest_covers_full_source_contiguously() {
    // Ranges across all destination pixels must partition the source
    // range exactly: no gaps, no overlaps, and the very first/last must
    // touch the source bounds -- otherwise a box-filter average would
    // silently skip or double-count source pixels.
    const int destSize = 300, srcSize = 1200;
    int prevEnd = 0;
    for (int i = 0; i < destSize; i++) {
        int start, end;
        sourceRangeForDest(i, destSize, srcSize, start, end);
        TEST_ASSERT_EQUAL_INT(prevEnd, start);
        TEST_ASSERT_TRUE(end > start);
        prevEnd = end;
    }
    TEST_ASSERT_EQUAL_INT(srcSize, prevEnd);
}

void test_source_range_for_dest_single_dest_pixel_spans_whole_source() {
    int start, end;
    sourceRangeForDest(0, 1, 1600, start, end);
    TEST_ASSERT_EQUAL_INT(0, start);
    TEST_ASSERT_EQUAL_INT(1600, end);
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
    RUN_TEST(test_source_range_for_dest_downscale);
    RUN_TEST(test_source_range_for_dest_no_downscale_is_single_pixel);
    RUN_TEST(test_source_range_for_dest_covers_full_source_contiguously);
    RUN_TEST(test_source_range_for_dest_single_dest_pixel_spans_whole_source);
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
