#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo
#include "logic/battery_curve.h"
#include "logic/wifi_strength.h"

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
// this per board, not hardcoded (see portal.cpp's rotOptions()).
constexpr bool PANEL_NATIVE_LANDSCAPE = TFT_WIDTH > TFT_HEIGHT;

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// Version-4 (33x33) QR centered at (cx, cy), scale px per module, with a
// 4-module white quiet zone. Draws into the sprite only. Payload must fit
// version 4 at ECC_LOW (78 bytes).
void drawQrCode(const String &text, int cx, int cy, int scale);

// Status-screen dashboard icons (battery/Wi-Fi/next-photo tiles). All
// built from fillRect/fillCircle/fillTriangle primitives -- no image
// assets, same approach drawQrCode already uses. Each is centered at
// (cx, cy) with r setting overall icon scale.
void drawBatteryIcon(int cx, int cy, int r, int pct, uint32_t fillColor, bool charging);
void drawWifiIcon(int cx, int baseY, int r, WifiStrength level);
void drawNextPhotoIcon(int cx, int cy, int r, bool pinned);

// Board-appropriate battery fill color: functional green/yellow/red on
// 6-color Spectra panels (EE02), solid black everywhere else -- the
// percentage number and bar fill length already carry the information on
// grayscale/mono panels, so no data is lost by dropping the color cue.
uint32_t batteryColorForLevel(BatteryLevel level);

// Board-appropriate charging-indicator color: blue on 6-color Spectra
// panels, black everywhere else.
uint32_t chargingBoltColor();

// Decode a baseline JPEG into PSRAM, Floyd-Steinberg dither it to the
// panel's palette, and write it into the sprite (no update()).
bool renderJpeg(uint8_t *buf, size_t len);

// For gray-capable panels (e.g. EE03): switch the sprite into
// USE_MUTIGRAY_EPAPER's gray mode. No-op on panels that don't support it.
// Call once after epaper.begin() / applyOrientation(), before drawing.
void initPanelColorMode();

// Full-panel error screen (calls update()).
void showError(const String &msg);
