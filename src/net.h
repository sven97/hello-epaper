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

// Auto firmware update. Call only at the end of a fully successful,
// unattended fetch cycle (photo already on the panel). Enforces the
// cadence and eligibility gates itself; on a decision to install, streams
// the new image into the passive OTA slot with MD5 verification, records
// the trial in NVS (ota_state.h), and reboots (does not return). Any
// failure logs one line and returns — never propagates. force=true skips
// only the cadence timer (for the portal's manual install) — every other
// gate still holds.
void maybeRunOtaCheck(int batteryPct, bool force = false);

// Portal manual update, two steps.
//
// otaPeek(): step 1 — fetch + parse the manifest and compare, without
// downloading or flashing. Fills `latestBuild` on UpToDate / Available.
// Resets the auto cadence timer (a manual check is still a check).
enum class OtaPeekResult { UpToDate, Available, Blocked, Unreachable };
OtaPeekResult otaPeek(uint32_t &latestBuild);

// otaInstallNow(): step 2 — re-validate against the manifest and, if a
// newer build is still there, download + flash + reboot (cadence timer
// bypassed; every other gate still applies). May not return. Safe to
// call from an HTTP handler.
void otaInstallNow();

// Detect the UTC offset from the network's public IP, then NTP-sync.
// Returns false if NTP never synced (offset may still be cached-stale).
bool syncClock();

// Set the TZ environment to a fixed UTC offset (seconds east of UTC).
// Needed at every boot: the TZ env does not survive deep sleep.
void applyUtcOffset(long offsetSec);
