#pragma once
#include <Arduino.h>

// Settings portal, live while the status screen is on the display.
// Lifecycle: connectWifi() -> startPortal() -> runPortal() -> caller
// redisplays the cached image and sleeps. Settings auto-save via /set as
// the user changes them; runPortal() only reports how it exited.
enum class PortalResult { KeyExit, Timeout };

bool startPortal();                                   // mDNS + HTTP :80
PortalResult runPortal(uint32_t inactivityTimeoutMs); // blocking loop
String portalUrl();                                   // "http://<name>.local"

// Dev mode (USB host attached, device never sleeps): the portal runs
// permanently. setPortalPersistent(true) makes runPortal() leave the
// server + mDNS up on exit; servicePortal() pumps requests (and cached-
// image re-renders) from loop(); takePortalFetch() reports (once) that an
// image-source change needs a fresh fetch — the caller runs doFetchCycle.
void setPortalPersistent(bool on);
void servicePortal();
bool takePortalFetch();

// Stop the web server + mDNS (no-op when not running). connectWifi()
// calls this before any connect that may open the WiFiManager captive
// portal — both servers want port 80. The dev-mode loop restarts the
// portal (fresh mDNS name included) once Wi-Fi is back.
void stopPortal();
bool portalIsRunning();
