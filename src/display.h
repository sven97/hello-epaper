#pragma once
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper for the selected combo

extern EPaper epaper;

// Apply the configured orientation (settings.rotation -> setRotation).
// Call once after epaper.begin(), before any drawing.
void applyOrientation();

// Decode a baseline JPEG into PSRAM, Floyd-Steinberg dither it to the
// panel's 6-color palette, and write it into the sprite (no update()).
bool renderJpeg(uint8_t *buf, size_t len);

// Full-panel error screen (calls update()).
void showError(const String &msg);
