#pragma once
// OTA trial + cadence state, NVS-backed.
//
// On the ESP32-S3, esp_restart() (RTC_SW_CPU_RST) clears RTC_DATA_ATTR —
// verified on hardware: bootCount comes back as 1 after an OTA reboot. The
// OTA path's whole job is to esp_restart() into a new image that the
// boot-health guard must then evaluate, so this state can NOT live in RTC.
// It lives in NVS ("frame" namespace, via the shared `prefs`), which
// survives esp_restart(), power loss, and deep sleep alike.
//
// Writes are rare: begin-trial + <=12 boot bumps + clear per OTA, and one
// last-check stamp per cadence interval — far inside NVS endurance.
// See docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include "state.h"
#include <stdint.h>

// NVS keys (<=15 chars): otaLastChk otaPend otaTrBoot otaTrFail
struct OtaState {
    uint32_t lastCheckEpoch; // wall-clock of the last manifest check
    uint32_t pendingBuild;   // build on trial; 0 => none
    uint8_t  trialBoots;     // wakes since the OTA reboot
    uint8_t  trialFetchFails;// consecutive failed cycles since
};

inline OtaState otaStateLoad() {
    return OtaState{
        prefs.getUInt("otaLastChk", 0),
        prefs.getUInt("otaPend", 0),
        prefs.getUChar("otaTrBoot", 0),
        prefs.getUChar("otaTrFail", 0),
    };
}

// Begin a trial: record the build we're about to flash into and zero the
// counters. Call BEFORE Update.writeStream() so a brownout mid-flash
// still leaves a coherent record for the next boot's guard.
inline void otaStateBeginTrial(uint32_t build) {
    prefs.putUInt("otaPend", build);
    prefs.putUChar("otaTrBoot", 0);
    prefs.putUChar("otaTrFail", 0);
}

// Trial resolved — confirmed good, reverted, or a flash that committed
// nothing — forget it.
inline void otaStateClearTrial() {
    prefs.remove("otaPend");
    prefs.remove("otaTrBoot");
    prefs.remove("otaTrFail");
}

inline void otaStateSetLastCheck(uint32_t epoch) {
    prefs.putUInt("otaLastChk", epoch);
}
inline void otaStateSetTrialBoots(uint8_t v) { prefs.putUChar("otaTrBoot", v); }

// A full fetch cycle failed while the trial image is the one running —
// count it toward the fetch-fail budget. No-op when not on trial or when
// the running build isn't the one on trial.
inline void otaStateNoteCycleFail() {
    uint32_t pend = prefs.getUInt("otaPend", 0);
    if (pend != 0 && pend == (uint32_t)FW_BUILD_NUMBER)
        prefs.putUChar("otaTrFail", (uint8_t)(prefs.getUChar("otaTrFail", 0) + 1));
}

// A full fetch cycle rendered a photo on the trial image — it works.
// Closes the trial; returns true when it actually did (for logging).
inline bool otaStateNoteCycleOk() {
    uint32_t pend = prefs.getUInt("otaPend", 0);
    if (pend != 0 && pend == (uint32_t)FW_BUILD_NUMBER) {
        otaStateClearTrial();
        return true;
    }
    return false;
}
