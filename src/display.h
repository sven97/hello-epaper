#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo

extern EPaper epaper;

// True if this panel's native (rotation 0) shape is wider than tall.
// EE02's native panel is portrait (1200x1600), but EE03/EE04/EE05's native
// panels are landscape (e.g. 800x480) -- rotation 0 does NOT universally
// mean "portrait", so the rotation dropdown's labels must be computed from
// this per board, not hardcoded (see portal.cpp's rotOptions()).
constexpr bool PANEL_NATIVE_LANDSCAPE = TFT_WIDTH > TFT_HEIGHT;

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// Version-4 (33x33) QR centered at (cx, cy), scale px per module, with a
// 4-module white quiet zone. Draws into the sprite only. Payload must fit
// version 4 at ECC_LOW (78 bytes).
void drawQrCode(const String &text, int cx, int cy, int scale);

// Decode a baseline JPEG into PSRAM, Floyd-Steinberg dither it to the
// panel's palette, and write it into the sprite (no update()).
bool renderJpeg(uint8_t *buf, size_t len);

// For gray-capable panels (e.g. EE03): switch the sprite into
// USE_MUTIGRAY_EPAPER's gray mode. No-op on panels that don't support it.
// Call once after epaper.begin() / applyOrientation(), before drawing.
void initPanelColorMode();

// Full-panel error screen (calls update()).
void showError(const String &msg);
