#include <unity.h>
#include "logic/layout_math.h"

void setUp() {}
void tearDown() {}

// ---- Column grid -----------------------------------------------------
void test_gridColX_is_zero_based_from_content_box() {
    TEST_ASSERT_EQUAL(0, gridColX(1));
    TEST_ASSERT_EQUAL(GRID_COL_W + GRID_GUTTER, gridColX(2));
    // Column 7 (start of the right half-window) sits after 6 col+gutter units.
    TEST_ASSERT_EQUAL(6 * (GRID_COL_W + GRID_GUTTER), gridColX(7));
}

void test_gridSpanW_full_width_matches_content_box() {
    TEST_ASSERT_EQUAL(GRID_CONTENT_W, gridSpanW(1, GRID_COLS));
}

void test_gridSpanW_half_window_plus_gutter_plus_half_is_full() {
    const int left = gridSpanW(1, 6);
    const int right = gridSpanW(7, 12);
    TEST_ASSERT_EQUAL(GRID_CONTENT_W, left + GRID_GUTTER + right);
    TEST_ASSERT_EQUAL(left, right); // symmetric split
}

void test_window_is_centred_on_the_1200px_panel() {
    TEST_ASSERT_EQUAL(1200, GRID_OUTER_MARGIN * 2 + GRID_WIN_W);
}

// ---- QR sizing -----------------------------------------------------
void test_qrScaleForBox_fits_inside_the_box() {
    const int box = gridSpanW(1, 5); // action-zone QR column
    const int s = qrScaleForBox(box, box);
    TEST_ASSERT_TRUE(s >= QR_MIN_SCALE);
    TEST_ASSERT_TRUE(QR_TOTAL_MODULES * s <= box);
}

void test_qrScaleForBox_never_below_floor() {
    TEST_ASSERT_EQUAL(QR_MIN_SCALE, qrScaleForBox(10, 10));
}

void test_qrScaleForBox_uses_the_smaller_dimension() {
    TEST_ASSERT_EQUAL(qrScaleForBox(1000, 100), qrScaleForBox(100, 1000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gridColX_is_zero_based_from_content_box);
    RUN_TEST(test_gridSpanW_full_width_matches_content_box);
    RUN_TEST(test_gridSpanW_half_window_plus_gutter_plus_half_is_full);
    RUN_TEST(test_window_is_centred_on_the_1200px_panel);
    RUN_TEST(test_qrScaleForBox_fits_inside_the_box);
    RUN_TEST(test_qrScaleForBox_never_below_floor);
    RUN_TEST(test_qrScaleForBox_uses_the_smaller_dimension);
    return UNITY_END();
}
