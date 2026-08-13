#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Live panel-capture debug routes (see
// docs/superpowers/specs/2026-08-13-panel-capture-design.md). Both read
// epaper's sprite via display.h's truePixelColor() -- NOT
// EPaper::readPixel(), which decodes through TFT_eSPI's generic
// _colorMap that this project's dithering never populates (see
// truePixelColor()'s declaration for why). truePixelColor() works
// uniformly across every supported panel's storage format
// (mono/gray/color), so no per-panel branching is needed here.

// GET /current: no storage. Streams a live, full-resolution BMP of
// whatever's currently in the sprite -- safe at full size because
// streaming needs no full-image buffer, just one row-sized scratch
// buffer. Sends its own 500 and returns false on a degenerate 0x0 panel
// size.
bool streamCurrentBmp(WebServer &server);

// Captures the sprite's CURRENT content (before it's about to be
// overwritten), downscaled to THUMBNAIL_MAX_LONG_SIDE, into a PSRAM
// buffer, replacing whatever was captured before. Thumbnail-sized
// (unlike streamCurrentBmp()) because this buffer persists in PSRAM
// until the next screen change, alongside other large PSRAM users (e.g.
// the JPEG decode buffer during a fetch) -- full resolution here risks
// PSRAM exhaustion. Call at the top of every function that's about to
// draw a new screen, before any drawing happens. A failed capture (e.g.
// PSRAM exhausted) silently leaves the previous snapshot in place.
void snapshotPrevious();

// GET /previous: streams the stored snapshot as image/bmp. Sends its own
// 404 and returns false if snapshotPrevious() has never been called.
bool streamPreviousBmp(WebServer &server);
