#pragma once
// Proportional screen layout math. Pure logic: host-testable, no Arduino
// deps. Line spacing is derived from the actual rendered font size (GFXFF
// font 4 is ~26px tall per its native size; setTextSize(N) scales that by
// N), not a fixed fraction of panel height. A fixed-height-fraction spacing
// broke on EE04/EE05's 800x480 default panel: bodySize stayed at its large
// tier (font stays ~52px tall) while height-fraction spacing shrank well
// below that, so lines overlapped each other outright, not just clipped at
// the edges. Deriving spacing from the font actually being drawn keeps
// text non-overlapping at any panel size.

// Native pixel height of GFXFF font 4 (see the LOAD_FONT4 comment in
// Seeed_GFX's Setup5xx headers: "Medium 26 pixel high font").
constexpr int FONT4_NATIVE_PX = 26;
// QR modules (33) plus the 4-module quiet zone on each side (drawQrCode).
constexpr int QR_MODULES_WITH_QUIET_ZONE = 33 + 8;

// Status-screen title/subtitle/chrome font sizes (yAdvance, taken directly
// from the vendored Fonts/GFXFF/FreeSans*.h headers) for the two size
// tiers. Large tier: FreeSansBold24pt7b / FreeSans18pt7b / FreeSans12pt7b.
// Small tier: FreeSansBold12pt7b / FreeSans9pt7b / classic Font2 (16px --
// there's no FreeSans size below 9pt vendored, and 9pt is already used for
// the subtitle on this tier, so chrome text falls back to the smaller
// classic bitmap font instead).
constexpr int TITLE_PX_LARGE = 56, TITLE_PX_SMALL = 29;
constexpr int SUB_PX_LARGE = 42, SUB_PX_SMALL = 22;
constexpr int CHROME_PX_LARGE = 29, CHROME_PX_SMALL = 16;

struct LayoutMetrics {
    int lineH;      // status/provisioning: body-line spacing (classic Font4)
    int smallLineH; // provisioning: caption-line spacing (classic Font4)
    int bodySize;   // setTextSize() for status-screen info lines
    int smallSize;  // setTextSize() for provisioning captions

    // Status screen (ui.cpp): title/subtitle/chrome font sizes for the
    // current tier, whether it's the two-column landscape layout, and the
    // draw positions for every element. Portrait uses leftCx == rightCx
    // (everything centered on one column); landscape splits them.
    bool landscape;
    int titleH, subH, chromeH, chromeLineH;
    int leftCx, rightCx;
    int qrScale;
    int titleY, subtitleY, info0Y, info1Y, info2Y;
    int captionY, urlY, qrCy, legend0Y, legend1Y, legend2Y;

    // Provisioning screen anchors (net.cpp), stacked top-down from actual
    // element sizes (not a fixed fraction of height) so nothing overlaps
    // regardless of panel size, plus an adaptively-sized QR scale.
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
    // content no matter how tight the spacing gets — drop to the smaller
    // tier instead.
    const bool large = shortSide >= 600;
    m.bodySize = large ? 2 : 1;
    m.smallSize = 1;
    m.lineH = lineHeightFor(m.bodySize);
    m.smallLineH = lineHeightFor(m.smallSize);

    m.titleH = large ? TITLE_PX_LARGE : TITLE_PX_SMALL;
    m.subH = large ? SUB_PX_LARGE : SUB_PX_SMALL;
    m.chromeH = large ? CHROME_PX_LARGE : CHROME_PX_SMALL;
    const int gap = m.titleH / 6;
    m.chromeLineH = m.chromeH * 4 / 3;

    m.landscape = width > height;

    if (!m.landscape) {
        // ---- Portrait: vertical stack, one column ----
        m.leftCx = m.rightCx = width / 2;
        const int headerH = m.titleH + gap + m.subH + 2 * gap;
        const int infoH = 3 * m.lineH;
        const int afterInfoGap = m.lineH / 3;
        const int legendH = 3 * m.chromeLineH;
        const int qrGap = m.lineH / 3;

        // Cap the QR at a sane fixed size instead of growing it to fill
        // whatever's left, and push the freed space to the top/bottom
        // margins -- same principle as the provisioning screen below, so a
        // physical picture-frame mat doesn't clip content at the edges.
        const int fixedH = headerH + infoH + afterInfoGap + m.chromeLineH +
                           m.chromeLineH + 2 * qrGap + legendH;
        const int naturalQrBudget = height > fixedH ? height - fixedH : 0;
        int scale = 4;
        while (scale > 1 && naturalQrBudget / QR_MODULES_WITH_QUIET_ZONE < scale)
            scale--;
        m.qrScale = scale;
        const int qrPx = QR_MODULES_WITH_QUIET_ZONE * scale;
        const int slack = naturalQrBudget > qrPx ? naturalQrBudget - qrPx : 0;
        const int margin = slack / 2;

        m.titleY = margin + m.titleH / 2;
        m.subtitleY = m.titleY + m.titleH / 2 + gap + m.subH / 2;
        m.info0Y = m.subtitleY + m.subH / 2 + 2 * gap + m.lineH / 2;
        m.info1Y = m.info0Y + m.lineH;
        m.info2Y = m.info1Y + m.lineH;
        m.captionY = m.info2Y + afterInfoGap;
        m.urlY = m.captionY + m.chromeLineH;
        m.qrCy = m.urlY + m.chromeLineH + qrGap + qrPx / 2;
        m.legend0Y = m.qrCy + qrPx / 2 + qrGap;
        m.legend1Y = m.legend0Y + m.chromeLineH;
        m.legend2Y = m.legend1Y + m.chromeLineH;
    } else {
        // ---- Landscape: two columns, QR on the right ----
        // Uses the panel's spare width instead of fighting for scarce
        // height -- on EE04/EE05's 800x480 default panel this is what
        // makes a full-size QR possible at all (a vertical stack there
        // only had room for a badly cramped QR).
        const int leftW = width * 58 / 100;
        const int rightW = width - leftW;
        m.leftCx = leftW / 2;
        m.rightCx = leftW + rightW / 2;

        const int leftContentH = m.titleH + gap + m.subH + 2 * gap + 3 * m.lineH;
        const int leftMargin = height > leftContentH ? (height - leftContentH) / 2 : 0;
        m.titleY = leftMargin + m.titleH / 2;
        m.subtitleY = m.titleY + m.titleH / 2 + gap + m.subH / 2;
        m.info0Y = m.subtitleY + m.subH / 2 + 2 * gap + m.lineH / 2;
        m.info1Y = m.info0Y + m.lineH;
        m.info2Y = m.info1Y + m.lineH;

        const int legendH = 3 * m.chromeLineH;
        const int qrGap = m.chromeLineH / 2;
        int scale = 4;
        while (scale > 1 &&
               m.chromeLineH + qrGap + QR_MODULES_WITH_QUIET_ZONE * scale + qrGap +
                       m.chromeLineH + m.chromeLineH / 2 + legendH >
                   height)
            scale--;
        m.qrScale = scale;
        const int qrPx = QR_MODULES_WITH_QUIET_ZONE * scale;
        const int rightContentH = m.chromeLineH + qrGap + qrPx + qrGap +
                                  m.chromeLineH + m.chromeLineH / 2 + legendH;
        const int rightMargin = height > rightContentH ? (height - rightContentH) / 2 : 0;

        m.captionY = rightMargin + m.chromeLineH / 2;
        m.qrCy = m.captionY + m.chromeLineH / 2 + qrGap + qrPx / 2;
        m.urlY = m.qrCy + qrPx / 2 + qrGap;
        m.legend0Y = m.urlY + m.chromeLineH / 2 + m.chromeLineH;
        m.legend1Y = m.legend0Y + m.chromeLineH;
        m.legend2Y = m.legend1Y + m.chromeLineH;
    }

    // Provisioning screen: 4 body lines + 3 caption lines of text, plus 2
    // QR codes, stacked top-down. Pick the largest QR scale (capped at 4,
    // matching the original fixed size on large panels) that still lets
    // everything fit within the panel height, then center the whole block
    // vertically the same way as the status screen above.
    const int textTotal = 4 * m.lineH + 3 * m.smallLineH;
    int scale = 4;
    while (scale > 1 &&
           textTotal + 2 * QR_MODULES_WITH_QUIET_ZONE * scale + m.lineH > height)
        scale--;
    m.provQrScale = scale;
    const int qrPx = QR_MODULES_WITH_QUIET_ZONE * scale;
    const int totalNeeded = textTotal + 2 * qrPx + m.lineH;
    const int provSlack = height > totalNeeded ? height - totalNeeded : 0;
    const int provMargin = provSlack / 2;

    int y = provMargin + m.lineH / 2;
    m.provTitleY = y;                y += m.lineH;
    m.provStep1Y = y;                y += m.lineH;
    m.provQr1Y = y + qrPx / 2;       y += qrPx + m.lineH / 2;
    m.provJoinManualY = y;           y += m.smallLineH;
    m.provStep2Y = y;                y += m.lineH;
    m.provQrHintY = y;               y += m.smallLineH;
    m.provQr2Y = y + qrPx / 2;       y += qrPx + m.lineH / 2;
    m.provStep3Y = y;                y += m.lineH;
    m.provChangeY = y;
    return m;
}
