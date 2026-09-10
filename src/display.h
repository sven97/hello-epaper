#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo
#include "icons.h"
#include "logic/status_content.h" // ScreenState, ErrorKind, ScreenContent

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

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// RAII scope guard for the unified status/onboarding/error screen. The
// EE02 panel is portrait-native, so nothing has to be swapped -- this is
// now a no-op kept only so the status/onboarding/error call sites don't
// churn. (Photo display still honours settings.rotation via
// applyOrientation().)
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

// Error card over the retained image. `kind` picks the framing: Wifi
// blanks the signal row ("Wi-Fi problem"), Image keeps the real SSID +
// signal ("Image source problem") since the network is fine. `msg` is the
// specific reason, shown in the action zone. Only drawn when someone is
// watching (button-initiated actions) -- unattended wakes keep the photo.
void showError(const String &msg, ErrorKind kind = ErrorKind::Wifi);
