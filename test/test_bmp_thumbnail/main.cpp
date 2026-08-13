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
