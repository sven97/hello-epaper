#pragma once
// Proportional screen layout math. Pure logic: host-testable, no Arduino
// deps. Line spacing is derived from the actual rendered font size (GFXFF
// font 4 is ~26px tall per its native size; setTextSize(N) scales that by
// N), not a fixed fraction of panel height -- a fixed-height-fraction
// spacing broke on EE04/EE05's 800x480 default panel: bodySize stayed at
// its large tier (font stays ~52px tall) while height-fraction spacing
// shrank well below that, so lines overlapped each other outright, not
// just clipped at the edges. Deriving spacing from the font actually being
// drawn keeps text non-overlapping at any panel size.
//
// The status screen (ui.cpp) uses one adaptive vertical stack -- title +
// board-model badge, a divider rule, a 3-tile dashboard row (battery/
// Wi-Fi/next-photo), a caption line, the settings QR, and a button legend
// -- for every panel size and orientation. There is deliberately no
// separate landscape (two-column) layout: a short landscape panel (e.g.
// EE04's 800x480) just shrinks the same stack via the existing
// scale-to-fit QR logic and font tiers below.

// Native pixel height of GFXFF font 4 (see the LOAD_FONT4 comment in
// Seeed_GFX's Setup5xx headers: "Medium 26 pixel high font").
constexpr int FONT4_NATIVE_PX = 26;
// QR modules (33) plus the 4-module quiet zone on each side (drawQrCode).
constexpr int QR_MODULES_WITH_QUIET_ZONE = 33 + 8;

// Status-screen font sizes (yAdvance, taken directly from the vendored
// Fonts/GFXFF/FreeSans*.h headers) for the two size tiers. Large tier:
// FreeSansBold24pt7b (title) / FreeSans18pt7b (tile value) / FreeSans12pt7b
// (chrome: badge/tile label/caption/scan/url/legend). Small tier:
// FreeSansBold12pt7b / FreeSans9pt7b / classic Font2 (16px -- there's no
// FreeSans size below 9pt vendored, and 9pt is already used for the tile
// value on this tier, so chrome text falls back to the smaller classic
// bitmap font instead).
constexpr int TITLE_PX_LARGE = 56, TITLE_PX_SMALL = 29;
constexpr int TILE_VALUE_PX_LARGE = 42, TILE_VALUE_PX_SMALL = 22;
constexpr int CHROME_PX_LARGE = 29, CHROME_PX_SMALL = 16;

struct LayoutMetrics {
    int lineH;      // provisioning: body-line spacing (classic Font4)
    int smallLineH; // provisioning: caption-line spacing (classic Font4)
    int bodySize;   // provisioning setTextSize(); also selects the status
                    // screen's font tier (2 = large, 1 = small)
    int smallSize;  // provisioning setTextSize() for captions

    // Status screen (ui.cpp): one adaptive vertical stack, used for every
    // panel size/orientation.
    int titleH, tileValueH, chromeH, chromeLineH;
    int cx;                                 // page horizontal center
    int marginX;                            // shared left/right margin
    int titleY;                             // title + badge mid-anchor y
    int ruleY;                              // header divider line y
    int tileTop, tileH, tileW, tileGap;     // tile box geometry
    int tile0Cx, tile1Cx, tile2Cx;          // tile center x positions
    int tileIconCy, tileValueY, tileLabelY; // vertical anchors inside a tile
    int captionY;
    int qrScale;
    int qrCy;
    int scanY, urlY, legend0Y, legend1Y, legend2Y;

    // Provisioning screen anchors (net.cpp) -- unchanged by this rework.
    int provTitleY, provStep1Y, provQr1Y, provJoinManualY, provStep2Y,
        provQrHintY, provQr2Y, provStep3Y, provChangeY;
    int provQrScale;
};

// 4/3 headroom: enough gap for ascenders/descenders between stacked lines
// at this font's native size, without wasting space on panels where every
// pixel of vertical room matters (see EE04/EE05 below).
inline int lineHeightFor(int textSize) {
    return FONT4_NATIVE_PX * textSize * 4 / 3;
}

inline LayoutMetrics computeLayout(int width, int height) {
    LayoutMetrics m{};
    const int shortSide = width < height ? width : height;
    // Below ~600px, a giant body font (2x) leaves no room for this screen's
    // content no matter how tight the spacing gets -- drop to the smaller
    // tier instead.
    const bool large = shortSide >= 600;
    m.bodySize = large ? 2 : 1;
    m.smallSize = 1;
    m.lineH = lineHeightFor(m.bodySize);
    m.smallLineH = lineHeightFor(m.smallSize);

    m.titleH = large ? TITLE_PX_LARGE : TITLE_PX_SMALL;
    m.tileValueH = large ? TILE_VALUE_PX_LARGE : TILE_VALUE_PX_SMALL;
    m.chromeH = large ? CHROME_PX_LARGE : CHROME_PX_SMALL;
    m.chromeLineH = m.chromeH * 4 / 3;
    const int gap = m.titleH / 6;

    m.cx = width / 2;
    m.marginX = width / 12;

    // ---- Tile row geometry: fixed once the font tier is known ----
    const int tilePad = m.chromeH / 3;
    const int iconSize = m.tileValueH;
    m.tileH = 2 * tilePad + iconSize + gap / 3 + m.tileValueH + gap / 3 + m.chromeH;
    const int rowW = width - 2 * m.marginX;
    m.tileGap = rowW / 20;
    m.tileW = (rowW - 2 * m.tileGap) / 3;
    m.tile0Cx = m.marginX + m.tileW / 2;
    m.tile1Cx = m.tile0Cx + m.tileW + m.tileGap;
    m.tile2Cx = m.tile1Cx + m.tileW + m.tileGap;

    // ---- Stack the whole screen top-to-bottom, capping the QR at a sane
    // size instead of growing it to fill whatever's left, and pushing the
    // freed space to the top/bottom margins -- same principle as the
    // provisioning screen below, so a physical picture-frame mat doesn't
    // clip content at the edges ----
    const int ruleGap = gap;          // header -> rule
    const int tileGapAbove = 2 * gap; // rule -> tile row
    const int captionGap = gap;       // tile row -> caption
    const int qrGap = m.lineH / 3;

    const int aboveQrH = m.titleH + ruleGap + tileGapAbove + m.tileH +
                        captionGap + m.chromeLineH;
    const int belowQrH = 5 * m.chromeLineH; // scan + url + 3 legend lines
    const int fixedH = aboveQrH + 2 * qrGap + belowQrH;

    const int naturalQrBudget = height > fixedH ? height - fixedH : 0;
    int scale = 4;
    while (scale > 1 && naturalQrBudget / QR_MODULES_WITH_QUIET_ZONE < scale)
        scale--;
    m.qrScale = scale;
    const int qrPx = QR_MODULES_WITH_QUIET_ZONE * scale;
    const int slack = naturalQrBudget > qrPx ? naturalQrBudget - qrPx : 0;
    const int margin = slack / 2;

    m.titleY = margin + m.titleH / 2;
    m.ruleY = m.titleY + m.titleH / 2 + ruleGap;
    m.tileTop = m.ruleY + tileGapAbove;
    m.tileIconCy = m.tileTop + tilePad + iconSize / 2;
    m.tileValueY = m.tileTop + tilePad + iconSize + gap / 3 + m.tileValueH / 2;
    m.tileLabelY = m.tileValueY + m.tileValueH / 2 + gap / 3 + m.chromeH / 2;
    m.captionY = m.tileTop + m.tileH + captionGap;
    m.qrCy = m.captionY + m.chromeLineH / 2 + qrGap + qrPx / 2;
    m.scanY = m.qrCy + qrPx / 2 + qrGap;
    m.urlY = m.scanY + m.chromeLineH;
    m.legend0Y = m.urlY + m.chromeLineH;
    m.legend1Y = m.legend0Y + m.chromeLineH;
    m.legend2Y = m.legend1Y + m.chromeLineH;

    // ---- Provisioning screen (net.cpp): 4 body lines + 3 caption lines of
    // text, plus 2 QR codes, stacked top-down. Pick the largest QR scale
    // (capped at 4, matching the original fixed size on large panels) that
    // still lets everything fit within the panel height, then center the
    // whole block vertically the same way as the status screen above.
    // Unchanged by this rework. ----
    const int textTotal = 4 * m.lineH + 3 * m.smallLineH;
    int pscale = 4;
    while (pscale > 1 &&
           textTotal + 2 * QR_MODULES_WITH_QUIET_ZONE * pscale + m.lineH > height)
        pscale--;
    m.provQrScale = pscale;
    const int provQrPx = QR_MODULES_WITH_QUIET_ZONE * pscale;
    const int totalNeeded = textTotal + 2 * provQrPx + m.lineH;
    const int provSlack = height > totalNeeded ? height - totalNeeded : 0;
    const int provMargin = provSlack / 2;

    int y = provMargin + m.lineH / 2;
    m.provTitleY = y;              y += m.lineH;
    m.provStep1Y = y;              y += m.lineH;
    m.provQr1Y = y + provQrPx / 2; y += provQrPx + m.lineH / 2;
    m.provJoinManualY = y;         y += m.smallLineH;
    m.provStep2Y = y;              y += m.lineH;
    m.provQrHintY = y;             y += m.smallLineH;
    m.provQr2Y = y + provQrPx / 2; y += provQrPx + m.lineH / 2;
    m.provStep3Y = y;              y += m.lineH;
    m.provChangeY = y;
    return m;
}
