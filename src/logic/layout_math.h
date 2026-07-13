#pragma once
// Proportional screen layout math. Pure logic: host-testable, no Arduino
// deps. All ratios are calibrated against the pixel positions the status
// and provisioning screens used to hardcode for the EE02 panel at its
// default portrait resolution (1200x1600) — computeLayout(1200, 1600)
// reproduces every one of those constants exactly. Scaling by height keeps
// both screens on-panel (no overflow) at any other resolution instead of
// only ever fitting 1200x1600.

struct LayoutMetrics {
    int lineH;      // status screen: size-2 (body) line spacing
    int smallLineH; // status screen: size-1 (caption) line spacing
    int legendTop;  // status screen: y of the first button-legend line
    int bodySize;   // setTextSize() for titles/body lines
    int smallSize;  // setTextSize() for captions

    // Provisioning screen anchors (net.cpp) — same relative positions as
    // the original fixed layout, scaled proportionally to height.
    int provTitleY, provStep1Y, provQr1Y, provJoinManualY, provStep2Y,
        provQrHintY, provQr2Y, provStep3Y, provChangeY;
};

inline LayoutMetrics computeLayout(int width, int height) {
    LayoutMetrics m{};
    m.lineH = height * 90 / 1600;
    m.smallLineH = height * 55 / 1600;
    m.legendTop = height - height * 200 / 1600;

    const int shortSide = width < height ? width : height;
    m.bodySize = shortSide >= 400 ? 2 : 1;
    m.smallSize = 1;

    m.provTitleY = height * 90 / 1600;
    m.provStep1Y = height * 210 / 1600;
    m.provQr1Y = height * 400 / 1600;
    m.provJoinManualY = height * 545 / 1600;
    m.provStep2Y = height * 660 / 1600;
    m.provQrHintY = height * 725 / 1600;
    m.provQr2Y = height * 880 / 1600;
    m.provStep3Y = height * 1060 / 1600;
    m.provChangeY = height * 1140 / 1600;
    return m;
}
