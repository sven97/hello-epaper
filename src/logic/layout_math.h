#pragma once
// Grid + QR geometry for the fixed 12-column status/onboarding/error
// screen on the EE02 panel (1200x1600, portrait-native). Pure logic:
// host-testable, no Arduino deps. See
// docs/superpowers/specs/2026-09-09-paperframe-ee02-only-status-redesign-design.md.
//
// This replaced the multi-panel content-fitting engine (font ladders,
// fitSize, the shrink pass, the reduction cascade) -- with exactly one
// panel of one fixed size the layout is hand-placed, not solved.

#include <cstdint>

// ---- QR: fixed version 4 (33x33 modules), a 4-module quiet zone each
// side, scaled px/module. QR_MIN_SCALE is the guaranteed-scannable floor
// -- a QR rendered smaller isn't degraded, it's unusable, and the URL is
// shown as text anyway.
constexpr int QR_MODULES = 33;
constexpr int QR_QUIET_MODULES = 4;
constexpr int QR_TOTAL_MODULES = QR_MODULES + 2 * QR_QUIET_MODULES; // 41
constexpr int QR_MIN_SCALE = 2;

// ---- 12-column grid inside the drawn window. All values are panel px;
// the panel is a fixed 1200 (w) x 1600 (h) (PANEL_W/PANEL_H in config.h).
constexpr int GRID_OUTER_MARGIN = 60;   // panel edge -> window border, all sides
constexpr int GRID_WIN_W = 1080;        // 1200 - 2*GRID_OUTER_MARGIN
constexpr int GRID_WIN_H = 1480;        // 1600 - 2*GRID_OUTER_MARGIN
constexpr int GRID_WIN_BORDER = 2;
constexpr int GRID_WIN_RADIUS = 24;
constexpr int GRID_PAD = 48;            // window border -> content box
constexpr int GRID_COLS = 12;
constexpr int GRID_COL_W = 60;
constexpr int GRID_GUTTER = 24;
constexpr int GRID_CONTENT_W =
    GRID_COLS * GRID_COL_W + (GRID_COLS - 1) * GRID_GUTTER; // 984
constexpr int GRID_ZONE_PAD = 40;      // vertical padding above/below each zone rule

// x of column `c` (1-based), relative to the content-box left edge.
inline int gridColX(int c) { return (c - 1) * (GRID_COL_W + GRID_GUTTER); }

// Pixel width of the span from column `from` to column `to`, inclusive
// (1-based). gridSpanW(1, 12) == GRID_CONTENT_W.
inline int gridSpanW(int from, int to) {
    return (to - from + 1) * GRID_COL_W + (to - from) * GRID_GUTTER;
}

// Largest whole px/module scale for a QR (including its 4-module quiet
// zone) that fits a boxW x boxH area, floored at QR_MIN_SCALE.
inline int qrScaleForBox(int boxW, int boxH) {
    const int box = boxW < boxH ? boxW : boxH;
    const int s = box / QR_TOTAL_MODULES;
    return s < QR_MIN_SCALE ? QR_MIN_SCALE : s;
}
