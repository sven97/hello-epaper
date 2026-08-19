#pragma once
#include <Arduino.h>

// Connect with saved credentials. With allowPortal (the default), first
// boot / stale credentials open the captive portal (AP_NAME) and the
// panel shows instructions. With allowPortal=false — unattended timer
// wakes — never touch the panel or open an AP: fail fast and return
// false so the caller can keep the current photo and retry next wake.
// forceRadioReset power-cycles the Wi-Fi radio (WiFi.mode OFF -> STA)
// before attempting to connect — never erases saved credentials, only
// clears any stuck radio/connection-manager state. The caller passes
// true once a wake has already failed at least once this outage (see
// doFetchCycle()'s fetchFailStreak).
bool connectWifi(bool allowPortal = true, bool forceRadioReset = false);

// Fetch a photo into the sprite, dithered. On failure fills err with a
// short user-facing message and draws nothing — the caller decides
// whether anyone is watching.
bool fetchImage(String &err);

// Detect the UTC offset from the network's public IP, then NTP-sync.
// Returns false if NTP never synced (offset may still be cached-stale).
bool syncClock();

// Set the TZ environment to a fixed UTC offset (seconds east of UTC).
// Needed at every boot: the TZ env does not survive deep sleep.
void applyUtcOffset(long offsetSec);
