#pragma once
// Grid + QR geometry for the status/onboarding/error overlay on the EE02
// panel (1200x1600, portrait-native). Pure logic: host-testable, no
// Arduino deps. See
// docs/superpowers/specs/2026-09-09-paperframe-ee02-only-status-redesign-design.md.
//
// The status screen is a centred card drawn *over* the retained image --
// not a full-screen page. This file is only the fixed geometry; with one
// panel of one size the layout is hand-placed, not solved.

#include <cstdint>

// ---- QR: fixed version 4 (33x33 modules), a 4-module quiet zone each
// side, scaled px/module. QR_MIN_SCALE is the guaranteed-scannable floor
// -- a QR rendered smaller isn't degraded, it's unusable, and the URL is
// shown as text anyway.
constexpr int QR_MODULES = 33;
constexpr int QR_QUIET_MODULES = 4;
constexpr int QR_TOTAL_MODULES = QR_MODULES + 2 * QR_QUIET_MODULES; // 41
constexpr int QR_MIN_SCALE = 2;

// ---- Centred 960x1056 card on the 1200x1600 panel. All values panel px.
constexpr int PANEL_GRID_W = 1200;
constexpr int PANEL_GRID_H = 1600;
constexpr int GRID_WIN_W = 960;
constexpr int GRID_WIN_H = 1056;
constexpr int GRID_MARGIN_X = (PANEL_GRID_W - GRID_WIN_W) / 2; // 120
constexpr int GRID_MARGIN_Y = (PANEL_GRID_H - GRID_WIN_H) / 2; // 272
constexpr int GRID_WIN_BORDER = 2;
constexpr int GRID_WIN_RADIUS = 24;

// ---- 12-column grid inside the card.
constexpr int GRID_PAD = 48;            // card border -> content box
constexpr int GRID_COLS = 12;
constexpr int GRID_COL_W = 50;
constexpr int GRID_GUTTER = 24;
constexpr int GRID_CONTENT_W =
    GRID_COLS * GRID_COL_W + (GRID_COLS - 1) * GRID_GUTTER; // 864
constexpr int GRID_CONTENT_H = GRID_WIN_H - 2 * GRID_PAD;   // 960
constexpr int GRID_ZONE_PAD = 26;      // vertical padding above/below each zone rule

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
