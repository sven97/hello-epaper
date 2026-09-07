#pragma once
// Auto-update eligibility gates. Pure logic: host-testable, no Arduino
// deps. See docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include <cstdint>

// Below this the OTA download+flash (~15-30 s of radio + flash writes) is
// too risky against a brownout -- skip and try again a day later.
constexpr int OTA_MIN_BATTERY_PCT = 40;

struct OtaGate {
    bool     enabled;      // settings.otaEnabled
    uint32_t deviceBuild;  // FW_BUILD_NUMBER (0 => no provenance)
    bool     deviceDirty;  // FW_GIT_HASH ends in "-dirty"
    bool     trialPending; // an OTA image is currently on trial
    int      batteryPct;   // batteryPercent(lastVbatMv)
};

// Manual portal check/install: the user asked, so the "auto-update"
// toggle doesn't apply -- but every safety gate still does.
inline bool canManualUpdate(const OtaGate &g) {
    return !g.deviceDirty && !g.trialPending
        && g.deviceBuild > 0 && g.batteryPct >= OTA_MIN_BATTERY_PCT;
}

// Worth fetching the manifest at all this wake? (Cadence is enforced
// separately by the lastCheckEpoch clock in the caller.)
inline bool shouldCheckForUpdate(const OtaGate &g) {
    return g.enabled && canManualUpdate(g);
}

// Given a parsed manifest build number, flash now?
inline bool shouldInstallUpdate(const OtaGate &g, uint32_t manifestBuild) {
    return shouldCheckForUpdate(g) && manifestBuild > g.deviceBuild;
}
