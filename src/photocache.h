#pragma once
#include <Arduino.h>
#include <WebServer.h>

// display.h pulls in TFT_eSPI, which #defines FS_NO_GLOBALS before FS.h
// is included -- that suppresses FS.h's `using fs::File;` etc., so this
// file and photocache.cpp spell out fs::File instead of the usually-bare
// File.

// Flash-cached copy of the last successfully fetched/rendered photo --
// the original downloaded JPEG bytes, not a per-panel dithered
// framebuffer (see
// docs/superpowers/specs/2026-08-13-debug-visibility-design.md).
// Backed by LittleFS on the existing, previously-unused `spiffs`
// partition; mounted lazily (format-on-first-use) by whichever of these
// functions is called first.

// Overwrites the cached photo with buf/len. Returns false on a mount or
// write failure; never leaves a partially-written file behind.
bool savePhotoCache(const uint8_t *buf, size_t len);

// True if a cached photo file exists and is non-empty.
bool hasCachedPhoto();

// Streams the cached photo to an HTTP client as image/jpeg. Sends its own
// 404 and returns false if there's no cached photo.
bool streamCachedPhoto(WebServer &server);

// Reads the cached photo back into memory and renders it via
// display.h's renderJpeg() (fillScreen + dither into the sprite; caller
// still owns epaper.update()). Returns false if there's no cached photo
// or it fails to decode.
bool renderCachedPhoto();
