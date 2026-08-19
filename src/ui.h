#pragma once
#include <Arduino.h>
#include "logic/status_content.h"

// "timer" (hourly refresh), "btn-info"/"btn-new-pic"/"btn-pin" (which
// function's button ended the sleep), or "power-on" (cold start: power
// switch, USB plug, RESET, or a fresh flash).
const char *wakeReason();

// Persist what the info page needs on non-fetch wakes. Must run on EVERY
// successful fetch — otherwise the page shows stale data for a photo it
// doesn't describe.
void recordFetchMetadata();

// Draws `state`'s unified frame (header/status/action/legend, see the
// design spec) into the sprite only -- caller wraps the call in a
// PortraitScope and calls epaper.update() afterward. Shared by all three
// screen call sites (drawStatusScreen/showProvisioningScreen/showError),
// each a thin wrapper: gather its own ScreenContent, call this.
void drawFrameScreen(ScreenState state, const ScreenContent &content);

// Battery/Wi-Fi/next-fetch fields shared by the status and error screens
// (onboarding has no Wi-Fi/next-fetch data yet -- net.cpp fills its own
// ScreenContent directly). vbatMv/deltaMv/haveDelta: the caller's own
// battery read.
ScreenContent gatherLiveContent(int32_t vbatMv, int32_t deltaMv, bool haveDelta);

// Full-screen status page: wake/battery/wifi/refresh info, the settings
// portal URL + QR code, and a button legend with live state. Draws,
// forces portrait for the duration, and calls epaper.update() itself.
void drawStatusScreen(int32_t vbatMv, int32_t deltaMv, bool haveDelta);
