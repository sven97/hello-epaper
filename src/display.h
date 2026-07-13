#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo

extern EPaper epaper;

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// Version-4 (33x33) QR centered at (cx, cy), scale px per module, with a
// 4-module white quiet zone. Draws into the sprite only. Payload must fit
// version 4 at ECC_LOW (78 bytes).
void drawQrCode(const String &text, int cx, int cy, int scale);

// Decode a baseline JPEG into PSRAM, Floyd-Steinberg dither it to the
// panel's 6-color palette, and write it into the sprite (no update()).
bool renderJpeg(uint8_t *buf, size_t len);

// Full-panel error screen (calls update()).
void showError(const String &msg);
