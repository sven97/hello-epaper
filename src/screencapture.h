#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Live panel-capture debug routes (see
// docs/superpowers/specs/2026-08-13-panel-capture-design.md). Both read
// epaper's sprite via readPixel(), which normalizes every supported
// panel's storage format (mono/gray/color) back to RGB565 -- no
// per-panel branching needed here.

// GET /current: no storage. Streams a live downscaled BMP of whatever's
// currently in the sprite. Sends its own 500 and returns false on a
// degenerate 0x0 panel size.
bool streamCurrentBmp(WebServer &server);

// Captures the sprite's CURRENT content (before it's about to be
// overwritten) into a PSRAM buffer, replacing whatever was captured
// before. Call at the top of every function that's about to draw a new
// screen, before any drawing happens. A failed capture (e.g. PSRAM
// exhausted) silently leaves the previous snapshot in place.
void snapshotPrevious();

// GET /previous: streams the stored snapshot as image/bmp. Sends its own
// 404 and returns false if snapshotPrevious() has never been called.
bool streamPreviousBmp(WebServer &server);
