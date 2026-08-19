#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo
#include "icons.h"

extern EPaper epaper;

// True RGB565 color at a sprite pixel, correctly reversing this
// project's palette-nibble/truthiness storage scheme (see ditherToPanel
// in display.cpp): drawPixel() there stores a masked/truthiness-tested
// PALETTE index directly, bypassing color matching entirely, so
// EPaper::readPixel() -- which decodes through TFT_eSPI's generic (and,
// for this storage scheme, unrelated and never-populated-by-us)
// _colorMap -- returns nonsense. Use this instead wherever a pixel needs
// to be read back as its true displayed color (e.g. screencapture.cpp).
uint16_t truePixelColor(int x, int y);

// True if this panel's native (rotation 0) shape is wider than tall.
// EE02's native panel is portrait (1200x1600), but EE03/EE04/EE05's native
// panels are landscape (e.g. 800x480) -- rotation 0 does NOT universally
// mean "portrait", so the rotation dropdown's labels must be computed from
// this per board, not hardcoded (see portal.cpp's rotOptions()), and the
// unified status screen's portrait-forcing (PortraitScope below) uses it
// to pick the right rotation value too.
constexpr bool PANEL_NATIVE_LANDSCAPE = TFT_WIDTH > TFT_HEIGHT;

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// RAII scope guard for the unified status/onboarding/error screen (see
// docs/superpowers/specs/2026-08-18-unified-status-screen-design.md):
// forces portrait regardless of settings.rotation for its lifetime, then
// restores the configured orientation on scope exit. Construct one
// around the draw + epaper.update() call for that screen -- restoring
// only after update() matters, since the sprite's own dimensions (and
// therefore what update() pushes) follow whatever rotation is active
// while it's drawn.
struct PortraitScope {
    PortraitScope();
    ~PortraitScope();
};

// Version-4 (33x33) QR centered at (cx, cy), scale px per module, with a
// 4-module white quiet zone. Draws into the sprite only. Payload must fit
// version 4 at ECC_LOW (78 bytes).
void drawQrCode(const String &text, int cx, int cy, int scale);

// Draws a status-screen icon (see icons.h, always ICON_W x ICON_H) with
// its top-left corner at (x, y). One fixed size regardless of the
// surrounding text's font tier -- callers position by the sizing
// engine's icon-column width, not by icon size.
void drawStatusIcon(const uint8_t *bitmap, int x, int y, uint32_t fgColor);

// Legend zone's small square keycap glyph ("1"/"2"/"3") -- a drawn
// rounded box + centered digit (classic Font2), not a bitmap: the digit
// itself is text, unlike battery/Wi-Fi/refresh which have no text
// equivalent.
void drawKeycap(const char *digit, int cx, int cy, int sizePx, uint32_t fgColor);

// Decode a baseline JPEG into PSRAM, Floyd-Steinberg dither it to the
// panel's palette, and write it into the sprite (no update()).
bool renderJpeg(uint8_t *buf, size_t len);

// For gray-capable panels (e.g. EE03): switch the sprite into
// USE_MUTIGRAY_EPAPER's gray mode. No-op on panels that don't support it.
// Call once after epaper.begin() / applyOrientation(), before drawing.
void initPanelColorMode();

// Full-panel error screen. Gathers live battery/Wi-Fi/next-fetch state
// (see ui.h's gatherLiveContent()) plus `msg`, then draws the unified
// frame's Error state and calls update(). Only drawn when someone is
// watching (button-initiated actions) -- unattended wakes keep the photo.
void showError(const String &msg);
