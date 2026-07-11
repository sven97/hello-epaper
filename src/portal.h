#pragma once
#include <Arduino.h>

// Settings portal, live while the status page is on the panel.
// Lifecycle: connectWifi() -> startPortal() -> runPortal() -> caller runs
// a fetch cycle and sleeps. Every exit path behaves the same; the enum is
// for logging (ForgetWifi's next fetch cycle reopens provisioning).
enum class PortalResult { KeyExit, Timeout, Saved, ForgetWifi };

bool startPortal();                                   // mDNS + HTTP :80
PortalResult runPortal(uint32_t inactivityTimeoutMs); // blocking loop
String portalUrl();                                   // "http://<name>.local"

// Dev mode (USB host attached, device never sleeps): the portal runs
// permanently. setPortalPersistent(true) makes runPortal() leave the
// server + mDNS up on exit; servicePortal() pumps requests from loop();
// takePortalAction() reports (once) that a handler asked to apply
// settings — the caller reapplies orientation/TZ and fetches.
void setPortalPersistent(bool on);
void servicePortal();
bool takePortalAction();
